# CanvasGfx12

The Direct3D 12 graphics backend for Canvas. CanvasGfx12 is loaded at runtime as a DLL plugin via `XCanvas::LoadPlugin` and exposes `XGfxDevice`, from which all GPU objects are created.

This document describes the current state of the design. CanvasGfx12 is still being developed; subsystems described here may be extended, restructured, or replaced as the project evolves.

## Prerequisites

CanvasGfx12 requires:

- **Windows 10 version 1909 or later** with a D3D12-capable GPU.
- **D3D12 Enhanced Barriers** support. The backend creates an `ID3D12Device10` and uses `D3D12_BARRIER_LAYOUT` / `ID3D12GraphicsCommandList7::Barrier`. Drivers that do not support Enhanced Barriers will fail at device creation.
- **D3D_FEATURE_LEVEL_11_0** as the minimum feature level.

The device is created with `D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, ...)`, which selects the default adapter. Tight-alignment support is queried at runtime and used opportunistically for smaller heap units when available.

## Overview

CanvasGfx12 translates the abstract Canvas graphics interfaces into D3D12 API calls. Its major responsibilities are:

- **Resource allocation and lifecycle** across GPU heaps, buffer pools, and deferred-release queues.
- **Automatic barrier management** via a DAG-based task graph that uses D3D12 Enhanced Barriers.
- **Frame rendering** through a deferred shading pipeline with a 5-target G-buffer, fullscreen composition, and UI overlay.
- **Asynchronous uploads** on a dedicated COPY queue with cross-queue fence synchronization.

All public types are [GEM](../../deps/GEM) interfaces. GEM is a lightweight COM-style object model that provides reference counting (`AddRef`/`Release`), interface discovery (`QueryInterface`), and 64-bit interface IDs. Smart pointers (`Gem::TGemPtr<T>`) automate lifetime management. Concrete classes declare their supported interfaces with the `BEGIN_GEM_INTERFACE_MAP` / `GEM_INTERFACE_ENTRY` / `END_GEM_INTERFACE_MAP` macros. The `TGfxElement<T>` template used throughout CanvasGfx12 is a convenience base class that wires up GEM interface mapping and registers the object with the `XCanvas` root. See the [Canvas SDK documentation](../../docs/CanvasSdk/CanvasSdk.md) for interface rules and the [GEM repository](../../deps/GEM) for the implementation.

## Plugin Entry Point

The DLL exports a `CreateCanvasPlugin` factory. CanvasCore calls this after `LoadPlugin` to obtain an `XCanvasPlugin` instance, which in turn creates `CDevice12` objects on demand. The plugin has no static registration; all wiring happens through GEM interface discovery.

## Life of a Frame

Before diving into subsystems, here is the end-to-end sequence for rendering a single frame. The application drives the outer loop; everything below `BeginFrame` happens inside CanvasGfx12.

```
Application                      CanvasGfx12
-----------                      -----------
pScene->Update(dt)               Scene graph propagates dirty transforms.
                                 SubmitRenderables walks the tree, calling
                                 SubmitForRender on the render queue for
                                 each node with a bound element.
                                   Lights  --> accumulated into per-frame array
                                   Meshes  --> queued in m_RenderableQueue
                                   UI nodes --> queued in m_UIRenderableQueue

pRenderQueue->BeginFrame(pSC)    Acquires the back buffer from the swap chain.
                                 Creates/resizes the G-buffer and depth buffer.
                                 Inserts the first GPU task: clear all G-buffer
                                 targets and the depth buffer, set viewport and
                                 scissor, bind the geometry PSO.

pRenderQueue->EndFrame()         Uploads per-frame constants (camera, lights).
                                 For each queued mesh instance:
                                   Upload per-object constants to upload ring.
                                   Allocate SRV descriptors for vertex streams
                                   and material textures.
                                   Insert a GPU task that declares buffer/texture
                                   usages and records the draw call.
                                 Insert the composition task:
                                   Transition G-buffers to shader-resource layout.
                                   Draw a fullscreen triangle with deferred lighting.
                                 Flush pending glyph atlas uploads.
                                 For each queued UI element:
                                   Insert a UI task (text or rect draw).

pRenderQueue->FlushAndPresent()  Wait on copy-queue fence (if uploads pending).
                                 Dispatch scene task graph:
                                   Compute fixup barriers (committed vs. assumed).
                                   Submit [FixupCL, WorkCL] via ECL.
                                 Dispatch UI task graph.
                                 Signal the render fence.
                                 Dispatch present task graph:
                                   Transition back buffer to COMMON.
                                 Signal the render fence again.
                                 Wait on DXGI frame-latency waitable object.
                                 Call IDXGISwapChain::Present.
                                 Reclaim completed upload ring space,
                                 retired buffers, and deferred GEM refs.
                                 Reset all three task graphs for the next frame.
```

A complete bootstrap sequence showing device creation, scene setup, and the render loop can be found in the [top-level README](../../README.md#getting-started).

## Device

`CDevice12` is the root object for the backend. It wraps an `ID3D12Device10` and owns the two major infrastructure pieces: the resource manager and the copy queue. It also serves as the factory for render queues, buffers, surfaces, materials, mesh data, and UI elements.

On initialization, CDevice12 creates the D3D12 device, queries adapter information, and in debug builds registers a validation-layer message callback that routes diagnostics through the Canvas logger.

### Ownership and Destruction Order

The application holds a `TGemPtr<XGfxDevice>` that ref-counts the device. The device is the last graphics object destroyed. Shutdown proceeds in this order:

1. **Render queues** are released first. `Uninitialize` drains the GPU by waiting on the render fence, runs `ProcessCompletedWork` to free deferred resources, unregisters the queue's timeline from the resource manager, and shuts down the per-queue upload ring.
2. **CDevice12 destructor** runs. It shuts down the copy queue, which drains its GPU work and unregisters its timeline. Then it calls `m_ResourceManager.Shutdown()`, which drains all remaining timelines, clears the buffer pool buckets, and breaks the `CDevice12 -> pool -> CBuffer12 -> CDevice12` reference cycle.
3. In debug builds, the D3D12 debug callback is unregistered before the logger is released.

Getting this order wrong causes D3D12 validation errors or leaks. The key constraint is that the copy queue must shut down before the resource manager because it defers releases through the manager's per-timeline queues.

Key factory methods:

| Method | Creates |
|---|---|
| `CreateRenderQueue` | `CRenderQueue12` |
| `CreateSurface` | `CSurface12` |
| `CreateBuffer` | `CBuffer12` |
| `CreateMeshData` | `CMeshData12` |
| `CreateMaterial` | `CMaterial12` |
| `AllocateStructuredBuffer` | Pooled buffer via resource manager, staged through copy queue |
| `UploadTextureRegion` | Texture upload via copy queue staging |

Memory usage types map directly to D3D12 heap types:

| Canvas Usage | D3D12 Heap Type |
|---|---|
| `DeviceLocal` | `DEFAULT` |
| `HostWrite` | `UPLOAD` |
| `HostRead` | `READBACK` |

All resources are created with `D3D12_RESOURCE_DESC1` and `D3D12_BARRIER_LAYOUT` for Enhanced Barriers compatibility. Debug builds tag every resource with a human-readable name for PIX and the validation layer.

### Initial Surface Layouts

The initial barrier layout assigned at creation depends on the heap type:

| Heap Type | Initial Layout |
|---|---|
| `DEFAULT` | `COMMON` |
| `UPLOAD` | `UNDEFINED` |
| `READBACK` | `UNDEFINED` |

Surfaces on the default heap start in COMMON, which is the layout required by copy-queue destinations. The task graph's first-use semantics handle the transition from COMMON to whatever layout the first task requires.

### Error Handling

Most factory methods return `Gem::Result`. On failure they return an error code such as `Gem::Result::Fail` or `Gem::Result::OutOfMemory`. The application should check every result; the `Gem::ThrowGemError` helper converts a failed result into a C++ exception for code paths that prefer exception-based control flow. D3D12 HRESULT failures are converted by `ThrowFailedHResult`.

## Resource Allocator

`CResourceAllocator` routes allocation requests through a tiered strategy. Small and medium allocations are placed into shared `ID3D12Heap` objects managed by buddy sub-allocators. Large allocations fall back to committed resources.

### Heap Pools

Two heap pools cover the sub-committed range:

| Pool | Unit Size | Heap Size | Use Case |
|---|---|---|---|
| **Small** | 256 B or 64 KB | 512 KB or 4 MB | Textures and buffers that fit in a single small heap |
| **Large** | 64 KB | 64 MB | Buffers and textures up to 64 MB |

The small pool's unit size depends on hardware support for tight alignment, queried via `D3D12_FEATURE_TIGHT_ALIGNMENT` at initialization. When tight alignment is available the small pool uses 256-byte units; otherwise it falls back to 64 KB.

### Allocation Routing

```
if size <= SmallHeapSize and alignment <= SmallUnitSize
    --> placed allocation in Small pool
else if size <= LargeHeapSize
    --> placed allocation in Large pool
else
    --> committed resource
```

Within each pool, the buddy allocator grows on demand by adding new heaps. When a heap becomes completely free its `ID3D12Heap` is moved to a per-pool free list capped at 8 entries, reducing D3D12 heap creation churn.

### Allocation Key Encoding

Every placed allocation carries a 64-bit key that encodes the buddy block start, allocator tier, and block size in units:

```
[sizeInUnits:24 | tier:8 | blockStart+1:32]
```

A key of zero means committed. The `+1` on `blockStart` reserves zero for the committed sentinel.

The destructor checks that all buddy blocks are free and logs an error if leaks are detected.

## Resource Manager

`CResourceManager` sits above the allocator and owns the buffer pool, fence timelines, and deferred-release queues. It is pinned by pointer from `CDevice12` and is not reference-counted, avoiding circular references.

### Fence Timelines

Each command queue registers an `ID3D12Fence` with the manager and receives a stable `TimelineId`. Deferred operations carry a lightweight `FenceToken`:

```cpp
struct FenceToken
{
    uint32_t TimelineId;
    UINT64   Value;
};
```

A `FenceTimeline` holds the fence COM pointer along with two deques: retired buffers and deferred GEM object references. Because each timeline has exactly one producer, values within each deque are monotonically increasing, so a front-to-back drain correctly processes completed entries.

### Buffer Pool

Short-lived vertex buffers are recycled through a power-of-2 bucketed pool:

| Property | Value |
|---|---|
| Minimum bucket | 256 B (2^8) |
| Maximum bucket | 64 KB (2^16) |
| Bucket count | 9 |
| Per-bucket capacity | 8 |

`AcquireBuffer` pops a buffer from the smallest bucket that satisfies the request. `RetireBuffer` enqueues the allocation to the appropriate fence timeline. `Reclaim` walks every timeline, compares the fence's completed value against each retired entry, and either recycles the buffer into its bucket or drops it when the bucket is full.

Deferred GEM references follow the same pattern: `DeferRelease` enqueues a reference; `Reclaim` releases it once the fence value has been reached.

**When to use DeferRelease**: call `DeferRelease` any time a GEM object is still referenced by in-flight GPU work. For example, when the render queue finishes recording a frame it defers release of UI vertex buffers and glyph atlas surfaces that the GPU will read during execution. Letting a `TGemPtr` go out of scope is only safe when the GPU has already finished consuming the object, which is usually only guaranteed after a fence wait.

### Thread Safety

All public methods acquire a mutex. To avoid deadlock when `CBuffer12` destructors call back into `Free`, the lock is released before destroying reclaimed objects. Thread-local scratch storage holds the objects until destruction completes outside the lock.

## Buffers

`CBuffer12` wraps an `ID3D12Resource` buffer and tracks allocation metadata so the correct cleanup path runs at destruction. When the buffer was sub-allocated from a buddy pool, the destructor decodes the allocation key and returns the block to the resource allocator. Committed allocations release the COM pointer and nothing more.

## Surfaces

`CSurface12` represents a GPU texture resource: a render target, shader resource, depth buffer, or swap chain back buffer. It inherits from `CTextureResource`, which provides per-subresource layout tracking for Enhanced Barriers.

### Layout Tracking

`SubresourceLayout` tracks the committed D3D12 barrier layout for every subresource of a texture. It is optimized for the common case where all subresources share a single layout:

- **Uniform mode** stores one layout value with no heap allocation.
- **Per-subresource mode** expands to a vector when different subresources diverge.
- `TryCollapse` checks whether all entries match and folds back to uniform mode.

The **committed layout** on `CSurface12::m_CurrentLayout` represents the layout the GPU will find the resource in at the start of the next `ExecuteCommandLists` scope. The task graph reads this state to compute fixup barriers and writes it back after dispatch to reflect the final layout left by the last task. Within a task graph, barrier resolution uses the inherited state table rather than the committed layout; the two are reconciled only at the ECL boundary.

Swap chain back buffers are special-cased: the surface holds a weak pointer to its owning `CSwapChain12` so it can be re-associated when DXGI rotates the back buffer index.

## GPU Task Graph

`CGpuTaskGraph` manages GPU synchronization within a single `ExecuteCommandLists` scope. Tasks form a directed acyclic graph where each task declares which resources it uses and which prior tasks it depends on. The graph resolves D3D12 Enhanced Barriers based on resource usage transitions.

### Design Principles

- Tasks are purely declarative. They describe resource usage, not commands.
- Recording functions are invoked after barriers are resolved, ensuring atomic barrier-then-work sequences.
- Dependencies must reference tasks already created in the same graph.
- Cross-ECL layout bridging is handled by fixup barriers at dispatch time.

### Implicit vs. Explicit Dependencies

By default, each task inherits its resource state table from the immediately preceding task in creation order. This implicit sequencing is sufficient when tasks execute in a simple pipeline: the geometry pass writes the G-buffer, then the composition pass reads it.

Explicit `AddDependency` is needed when a task depends on a task other than its immediate predecessor, for example when two independent passes both produce data that a third pass consumes. Without explicit dependencies the third task would only inherit state from the second pass and might miss barriers required by the first.

### What Happens When Declarations Are Wrong

If a task reads a resource without declaring the usage, no barrier is emitted for that resource. The GPU may read stale data or observe a layout mismatch, depending on what prior tasks did. The D3D12 validation layer will report Enhanced Barrier errors in debug builds when it detects an access that does not match the current layout. The task graph's own `ValidateDependencyConflicts` checks for a different class of bug: two explicit dependencies that both write the same resource or leave it in conflicting layouts. It does not detect missing declarations.

### Resource Usage Declarations

Each task declares its texture and buffer usage:

**GpuTextureUsage** specifies the required layout, sync point, access type, and subresource granularity. Write and read semantics are inferred from the `D3D12_BARRIER_ACCESS` bits.

**GpuBufferUsage** specifies sync, access, and an optional byte range. Buffers have no layout in D3D12, so only sync and access matter.

Convenience helpers cover common patterns:

```cpp
graph.DeclareTextureUsage(task, pSurface,
    D3D12_BARRIER_LAYOUT_RENDER_TARGET,
    D3D12_BARRIER_SYNC_RENDER_TARGET,
    D3D12_BARRIER_ACCESS_RENDER_TARGET);

graph.DeclareBufferUsage(task, pBuffer,
    D3D12_BARRIER_SYNC_VERTEX_SHADING,
    D3D12_BARRIER_ACCESS_VERTEX_BUFFER);
```

### Inherited Resource State Tables

Each task carries a sorted table of `ResourceStateEntry` records keyed by `(pResource, Subresource)`. When a task is inserted:

1. The table starts as a copy of the previous task's final state.
2. States from explicit dependencies are unioned in by OR-ing sync and access bits.
3. The task's own declared usages overwrite the inherited entries.

The table is sorted so that per-subresource entries for a given resource appear before the uniform fallback entry. This lets lookups try an exact subresource match first and fall back to the uniform entry.

### Barrier Resolution

When `InsertTask` is called, the graph compares each declared usage against the inherited state:

- **Texture barriers** are needed when the layout changes, a prior write must be made visible, or the current access is a write.
- **Buffer barriers** are needed when the prior access was a write or the current access is a write.

If a resource has never been seen in this ECL, the task's required layout is assumed as the initial state and recorded in `m_ExpectedInitialLayouts` for fixup barrier computation.

### Fixup Barriers

At dispatch time the graph compares each surface's committed layout against the assumed initial layout. Mismatches produce fixup barriers recorded into a separate fixup command list. The queue submits `[FixupCL, WorkCL]` in a single `ExecuteCommandLists` call so fixups execute before the work that depends on them.

After submission, `ComputeFinalLayouts` walks all tasks in creation order to determine the last layout used for each surface and updates the committed state on the `CSurface12` objects.

### Dependency Validation

In debug configurations, `ValidateDependencyConflicts` checks that multiple dependencies do not both write to the same resource or leave a texture in conflicting layouts. Violations are recorded in `GetLastError`.

### Lifecycle

1. `Init` creates the work and fixup command lists and pulls initial allocators from the pool.
2. Tasks are created, decorated with usages and dependencies, then inserted.
3. `Dispatch` closes the work CL, computes and emits fixup barriers, submits both CLs, and updates committed layouts.
4. `Reset` returns allocators to the pool, resets the CLs, and clears the task list for the next frame.
5. `ReleaseMemory` releases all owned D3D12 objects for shutdown or scene transitions.

### RecordFunc Capture Rules

The `RecordFunc` lambda on a `CGpuTask` is invoked inline by `InsertTask` while the caller's stack is still alive. Capture only raw pointers, GPU virtual addresses, and plain-old-data values. Do **not** capture `TGemPtr`, `GfxResourceAllocation`, or other ref-counting smart pointers. The lambda object lives in the task deque and survives past `Reset`, so a captured smart pointer creates a hidden reference that keeps the resource alive indefinitely, causing a leak. The caller's stack already keeps the objects alive for the duration of `InsertTask`; capturing raw pointers is safe because the lambda is never called again after that point.

## Render Queue

`CRenderQueue12` wraps a D3D12 DIRECT command queue and orchestrates the per-frame rendering pipeline. It is single-threaded by design; all methods must be called from one thread.

### Frame Flow

```
BeginFrame        Set up G-buffer pass, clear targets, open UI graph
  SubmitForRender   Enqueue scene graph nodes (meshes, lights)
  SubmitForUIRender Enqueue UI graph nodes (text, rectangles)
EndFrame          Draw geometry, compose lighting, draw UI
FlushAndPresent   Dispatch task graphs, present, reclaim resources
```

These methods must be called in this order. `BeginFrame` stores the current swap chain and opens the task graphs; calling `SubmitForRender` or `EndFrame` without a preceding `BeginFrame` will dereference a null swap chain pointer. `FlushAndPresent` expects `EndFrame` to have finalized the renderable queues. The single-threaded design means there is no locking on these paths; calling them from multiple threads causes undefined behavior.

### Task Graphs

The render queue owns three independent `CGpuTaskGraph` instances:

| Graph | Purpose |
|---|---|
| **Scene** | Geometry pass, per-object draws, deferred composition |
| **UI** | Text and rectangle overlay draws |
| **Present** | Back buffer transition to COMMON for DXGI presentation |

Each graph has its own work and fixup command lists and pulls allocators from a shared `CCommandAllocatorPool`.

The split into three graphs is a design choice, not a correctness requirement. Each graph maps to one `ExecuteCommandLists` call, and the D3D12 spec guarantees that ECL submissions on the same queue execute in order. Separating them keeps each graph's barrier-resolution scope small and allows the scene and UI graphs to be dispatched independently. The present graph exists only to emit the back-buffer transition to COMMON, which must happen after UI drawing is complete.

### Descriptor Heap Management

Four descriptor heaps are created at initialization:

| Heap | Type | Count | Visibility |
|---|---|---|---|
| CBV/SRV/UAV | Shader resource | 65,536 | Shader-visible |
| Sampler | Sampler | 1,024 | Shader-visible |
| RTV | Render target view | 64 | CPU-only |
| DSV | Depth stencil view | 64 | CPU-only |

The shader-visible heap is used as a cycling allocator: `m_NextSRVSlot` advances with each draw and wraps at 65,536. Each mesh draw allocates 13 contiguous descriptors: 2 CBVs, 9 SRVs, and 2 UAVs. RTV and DSV slots wrap at 64.

The cycling allocator does not check for overflow within a single frame. If a frame exceeds roughly 5,000 mesh draws the slot counter wraps and overwrites descriptors from earlier in the same frame. In practice this has not been an issue at current scene complexity, but it is a known limitation. A future improvement could add a per-frame high-water-mark check or switch to a more robust ring allocator.

### Root Signatures and Pipeline States

All PSOs are created lazily on first use and cached for the lifetime of the render queue.

**Geometry pass** (default PSO):
- Slot 0: root CBV at b0 for per-frame constants.
- Slot 1: root SRV at t0 for the position stream.
- Slot 2: root UAV at u0, reserved.
- Slot 3: descriptor table covering CBV[2] at b1-b2, SRV[9] at t1-t9, UAV[2] at u1-u2.
- Static sampler s0: linear wrap with anisotropic mip filtering.
- No input layout. Vertices are fetched from structured buffers using `SV_VertexID`.
- 5 render targets matching the G-buffer formats. D32_Float depth with reverse-Z.

**Composition pass**:
- Slot 0: root CBV at b0 for per-frame constants (camera, light count, tile-grid dimensions, shadow atlas parameters, sky/background parameters).
- Slot 1: descriptor table with SRV[8] at t0-t7: G-buffer (normals t0, diffuse t1, world position t2), optional skybox cube A (t3), optional skybox cube B (t4), optional stars cube (t5), optional moon billboard texture (t6), optional shadow atlas (t7). Unbound slots hold null SRVs; shader flags select active branches.
- Slot 2: root SRV at t8 for `StructuredBuffer<HlslLight>` (the per-frame light table, sized to `PerFrame.LightCount`). Always bound; a one-element dummy is uploaded when no lights are visible so the slot is never null.
- Slot 3: root SRV at t9 for `StructuredBuffer<uint>` — per-tile light offsets, sized to `totalTiles + 1`. Tile `t`'s packed-index range is `[offsets[t], offsets[t + 1])`; `offsets[totalTiles]` is the total packed-index count.
- Slot 4: root SRV at t10 for `StructuredBuffer<uint>` — packed per-tile light indices, length = `offsets[totalTiles]`. Both tile buffers are rebuilt by `BuildTileLightLists` each frame and uploaded into the ring; a one-element dummy is bound when the framebuffer has no tiles.
- Slot 5: root SRV at t11 for `StructuredBuffer<uint>` — indices of always-on lights (ambient / directional / area), sized to `PerFrame.AlwaysOnLightCount`. The composite PS iterates these once per lit pixel ahead of the per-tile spatial loop.
- Static samplers: s0 = point/clamp (G-buffer texel fetch), s1 = linear/wrap (cubemap / stars sampling), s2 = linear/clamp (moon billboard), s3 = hardware PCF comparison sampler (GREATER_EQUAL reverse-Z, OPAQUE_WHITE border for shadow atlas).
- Single render target at back-buffer format. No depth.

**Text pass**:
- Slot 0: root CBV at b0 for `HlslTextConstants` (screen size, element offset, text color).
- Slot 1: root SRV at t0 for `StructuredBuffer<HlslGlyphInstance>` (one entry per visible glyph).
- Slot 2: descriptor table with SRV[1] at t1 for the SDF glyph atlas.
- Static sampler s0: linear clamp.
- Alpha blending: `SRC_ALPHA / INV_SRC_ALPHA`.
- The vertex shader expands each glyph instance to a 6-vertex quad using `SV_VertexID`.

**Rect pass**:
- Slot 0: root CBV at b0 for `HlslRectConstants` (screen size, element offset, rect size, fill color).
- No vertex buffer — the vertex shader derives the quad from `SV_VertexID` and the constants.
- Alpha blending: `SRC_ALPHA / INV_SRC_ALPHA`.

### G-Buffer

The geometry pass writes to five render targets simultaneously:

| Target | Format | Content |
|---|---|---|
| 0 | R10G10B10A2_UNorm | World-space normals (encoded to [0,1]) |
| 1 | R10G10B10A2_UNorm | Diffuse albedo color |
| 2 | R16G16B16A16_Float | World-space position, coverage flag in alpha |
| 3 | R8G8B8A8_UNorm | Roughness (R), metallic (G), ambient occlusion (B) |
| 4 | R11G11B10_Float | Linear emissive color |

A D32_Float depth buffer uses reverse-Z with `GREATER_EQUAL` comparison. In reverse-Z the near plane maps to 1.0 and the far plane to 0.0, which distributes floating-point precision more evenly across the depth range and virtually eliminates z-fighting at large distances. G-buffers are created lazily and resized when the swap chain dimensions change.

### Geometry Drawing

For each mesh instance the render queue:

1. Looks up the per-material-group vertex streams (position, normal, UV0, tangent).
2. Resolves the material and gathers its textures by role.
3. Builds material flags that enable or disable uber-shader branches.
4. Uploads per-object constants to the upload ring.
5. Allocates 13 descriptors and populates them with CBVs for the per-object constants, SRVs for vertex streams and textures, and null UAV placeholders.
6. Creates a GPU task that declares buffer and texture usages, then records the draw call.

Positions are bound as a root SRV at t0 by GPU virtual address. Remaining vertex streams and material textures are bound through the descriptor table.

### Displaced Mesh Drawing (Tessellation Path)

When a mesh instance has a material with a `GfxDisplacementDesc` attached (`XGfxMaterial::SetDisplacement`) and the mesh uses `PatchList4CP` topology, the render queue routes it through the displaced-mesh pipeline instead of the standard geometry PSO. This path uses a VS+HS+DS+PS quad-patch pipeline; the VS reads per-CP positions, UVs, and base normals from `StructuredBuffer<float4>`/`StructuredBuffer<float2>`/`StructuredBuffer<float4>` SRVs (t4 / t5 / t6) backed by the mesh's own vertex buffers — no IA bindings.

The displaced PSO root signature:
- Slot 0: root CBV at b0 for per-frame constants.
- Slot 1: root CBV at b1 for per-tile `HlslDisplacedConstants` (per-tile world transform, `MapScale`, `MapBias`).
- Slot 2: descriptor table with SRV[1]: displacement-map texture (t0). Visibility ALL (HS samples for curvature LOD, DS samples for the displacement offset).
- Slot 3: descriptor table with SRV[3]: albedo / AO / roughness atlases (t1..t3). Visibility PIXEL.
- Slot 4: descriptor table with SRV[3]: per-CP position + UV0 + normal StructuredBuffer SRVs (t4..t6). Visibility VERTEX.
- Static sampler s0: linear/clamp for displacement-map and atlas sampling.

`HlslDisplacedConstants` is uploaded to the upload ring once per displaced draw. The displacement-map surface must be in `LAYOUT_SHADER_RESOURCE` before the draw (use `XGfxRenderQueue::FinalizeUploadAsShaderResource` after the initial texture upload).

The per-draw world tile rect for shadow / cull aggregation is derived by the engine from the mesh's `XGfxMeshData::GetLocalBounds` plus the owning node's world translation — `GfxDisplacementDesc` carries no world or geometry fields. The CP positions in the mesh's vertex buffer are interpreted in mesh-local space; the per-tile CB's `World` matrix transforms positions (and rotates normals) to world space in the VS. CP UVs are pre-baked in D3D texture-UV space pointing into whatever sub-rect of the bound displacement map the mesh samples (meshes sharing a single displacement map carry per-mesh UV sub-rects this way). CP normals are the world-space directions along which the displacement offsets each CP; supply `(0, 0, 1, 0)` for a classic flat XY-plane heightfield.

The HS computes per-edge tessellation factors using a distance × curvature LOD scheme: the distance term is proportional to `edgeWorldLength / distToMidpoint`, and the curvature term samples a coarse mip of the displacement map for the Laplacian second derivative. The DS bilerps the patch CPs' base positions, UVs, and normals at the output vertex, samples the displacement map for a world-unit scalar, and emits `basePos + sample * baseNormal` as the displaced world position. Per-vertex normals come from the patch's tangent basis (CP0→CP1 spans U, CP0→CP3 spans V) and the displacement-map gradient — the formulation collapses to the classic Z-only "heightfield" result when all CP normals point +Z and the patch sits in the XY plane. The PS samples the material atlases and writes the standard G-buffer layout, so the deferred lighting and composition passes are unchanged.

The displaced shadow PSO shares the VS and HS bytecode (ensuring shadow tessellation matches the scene LOD to prevent Peter Panning) and pairs them with `DSDisplacedShadow`, which writes only `SV_Position` from a `HlslShadowConstants` matrix with no pixel shader.

### Light Submission

Lights are accumulated during scene graph traversal (gated by the LightBVH-driven visibility filter that `XScene::SubmitRenderables` installs via `XGfxRenderQueue::SetVisibleLights`). Each light's attenuation and cull distance are pre-computed on the CPU and packed into an `HlslLight` struct. The packed lights are uploaded each frame as a `StructuredBuffer<HlslLight>` (bound at root slot 2 / shader register `t8`), sized to the actual visible-light count — no fixed cap; the count scales with scene complexity the same way mesh-instance count does.

After SubmitLight aggregation and `ResolveShadowCasters`, `BuildTileLightLists` divides the framebuffer into `LIGHT_TILE_SIZE_PIXELS` × `LIGHT_TILE_SIZE_PIXELS` (32×32) screen tiles. It precomputes per-row / per-column sub-frustum planes from the camera view-projection, projects each spatial light's influence AABB (point lights use the cutoff-sphere bound, spot lights the tighter cone bound from `Math::Cone::ComputeAABB`) to a screen-tile rectangle, and confirms each candidate tile with a 6-plane test. Always-on lights (ambient / directional / area) bypass binning entirely and are uploaded as a flat `AlwaysOnLightIndices` buffer iterated once per pixel ahead of the per-tile loop. The per-tile result is written as a `(offsets[totalTiles + 1], packed-indices[Σ counts])` pair — no fixed cap, no padding — and uploaded as two `StructuredBuffer<uint>`s (root slots 3 / 4 at `t9` / `t10`). The composite shader picks the owning tile from `SV_Position` and iterates `[offsets[t], offsets[t + 1])`, turning the per-pixel cost from O(visible lights) into O(tile lights).

For directional lights with `LightFlags::CastsShadows`, `SubmitLight` additionally:

1. Lazily allocates the shadow atlas (one `D32_Float` surface with both `DepthStencil` and `ShaderResource` usage, 4096 px per side, divided into a fixed 2×2 grid of 2048² tiles — up to four shadow casters per frame).
2. Picks the next free atlas tile.
3. Builds a texel-snapped, reverse-Z orthographic `world -> shadow-clip` matrix via `BuildDirectionalShadowMatrix`, centring the frustum on the active camera so receivers near the viewer fall into the high-resolution slab. Snapping the focus point to the atlas texel grid removes shimmer when the camera moves.
4. Writes `ShadowAtlasRectUV`, `ShadowViewProj`, `ShadowFlags`, `ShadowDepthBias`, and `ShadowNormalOffsetTexels` into the light's `HlslLight` entry so the composite can sample without additional CPU plumbing.
5. Queues a `PendingShadowCaster` record for `EndFrame` to drain.

`EndFrame` then inserts one `ShadowPass_Displaced` `GpuTask` per (shadow caster × ready displaced tile), each declaring DSV-write usage on the atlas and SRV-read usage on the displacement map. The first task into a tile clears it with a scissor-restricted `ClearDepthStencilView(0.0f)` (reverse-Z far plane); subsequent tasks accumulate depth. The composite task downstream declares SRV usage on the atlas, so the `DEPTH_STENCIL_WRITE -> SHADER_RESOURCE` transition is emitted automatically by the task graph.

The shadow pass uses a dedicated depth-only PSO (`m_pDisplacedShadowPSO`) that reuses the existing `VSDisplaced` + `HSDisplaced` bytecode (so shadow tessellation tracks the camera and matches receiver LOD, preventing Peter Panning) and pairs them with `DSDisplacedShadow` (writes only `SV_Position` from a `b2` `HlslShadowConstants` matrix) with no pixel shader. Conservative caster-side `DepthBias` + `SlopeScaledDepthBias` are baked into the PSO; per-light caster bias values from `XLight::SetShadowDepthBias` are not honoured in v1 (only the receiver-side constant + normal-offset terms vary per light).

### Composition

After all geometry tasks are inserted, the render queue transitions the G-buffer surfaces from render target to shader resource layout and inserts a composition task. The composition shader owns every output pixel on the back buffer: G-buffer pixels receive deferred lighting; pixels with no geometry (normal alpha == 0) are filled by the scene background.

Three of the five G-buffer targets are bound as SRVs for the lighting path:

| SRV Slot | G-Buffer Target |
|---|---|
| t0 | Normals |
| t1 | Diffuse color |
| t2 | World position |

The PBR and emissive targets are written by the geometry pass but are not yet consumed by the composition shader. They are reserved for a future physically-based lighting pass that will incorporate roughness, metallic, AO, and emissive terms. The current composition shader performs Lambertian diffuse lighting only.

The composition task binds the composition PSO, sets the back buffer as the single render target, and draws a fullscreen triangle. The pixel shader decodes normals, loops over the light array, accumulates contributions, applies an exposure multiplier, and writes the final color.

### Background and Skybox

For pixels with no geometry, the composition shader falls back to the scene background, configured per-scene via `XScene::SetBackground` (which the render queue picks up via `XGfxRenderQueue::SetBackground`). The background is described by `GfxBackgroundDesc`:

- **Solid color fallback**: when no skybox is bound, the scene fills empty pixels with `SolidColor`. This is the default for newly created scenes (opaque black).
- **Skybox cubemap**: when `pSkyboxCubemapA` is set, empty pixels are filled by sampling the cubemap in the world-space view direction, rotated by the inverse of `Orientation`. `Intensity` is a linear multiplier on the sample.
- **Cubemap crossfade**: when both `pSkyboxCubemapA` and `pSkyboxCubemapB` are set, the result is `lerp(A, B, BlendFactor)`, enabling smooth transitions between authored sky presets (day → dusk → night) without per-frame CPU rebaking.
- **Stars cubemap**: an optional RGBA cubemap (`pStarsCubemap`) is additively blended over the skybox, rotated independently by `StarsOrientation`. The lower hemisphere (view direction z < 0) is clipped so stars never appear below the ground. Apps drive sidereal motion by updating `StarsOrientation` each frame.
- **Procedural sun disc**: rendered as a soft-edged disc in the composite shader, driven by `SunDirection` (the same unit vector the app uses for the sun's directional light, keeping a single source of truth for the sun's position). Enabled when `SunColor.a > 0`.
- **Moon billboard**: an optional 2D texture (`pMoonTexture`) rendered as a billboard centred on `MoonDirection` within an angular disc of radius `MoonAngularRadius`. Both celestial bodies are naturally clipped below the horizon.

All background parameters are packed into `HlslPerFrameConstants` and uploaded once per frame alongside the light array.

Shadow sampling lives in this same loop. The directional-light branch calls `SampleDirectionalShadow(light, P, N)` whenever `light.ShadowFlags & SHADOW_FLAG_HAS_SHADOW` is set. The helper:

1. Pushes `P` along `N` by `ShadowNormalOffsetTexels` (in shadow-atlas texels, converted to world meters via `ShadowPcfTexelStep / tileUvWidth`) — combats grazing-angle acne that pure depth bias cannot remove.
2. Projects the biased position through `light.ShadowViewProj` and remaps NDC.xy into atlas UV via `light.ShadowAtlasRectUV`.
3. Adds `ShadowDepthBias` to the projected NDC z (receiver-side bias in reverse-Z space).
4. Does a 2×2 hardware PCF lookup with a `SamplerComparisonState` (s3) configured `GREATER_EQUAL` (reverse-Z) and `D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE` so receivers outside the shadow frustum read as fully lit.

The shadow atlas itself is bound at `t7`. When no shadow casters exist in a frame, a null SRV is bound there and every light's `ShadowFlags == 0`, so the PCF path is short-circuited.

### UI Drawing

After the scene graph, the render queue processes the UI renderable queue. Text elements are drawn with the text PSO using the SDF glyph atlas. Rectangle elements use the rect PSO with geometry derived entirely from `SV_VertexID` and per-draw constants (rect size and fill color); no vertex buffer is bound. Both use alpha blending onto the back buffer.

### Frame Submission

`Flush` performs the following steps:

1. Waits on any pending copy-queue fence token so uploads are visible.
2. Dispatches the scene task graph.
3. Dispatches the UI task graph.
4. Signals the render fence.
5. Marks the upload ring submission boundary.
6. Resets both task graphs for the next frame.

`FlushAndPresent` calls `Flush`, then dispatches the present task graph to transition the back buffer to COMMON, waits on the DXGI frame latency waitable object, calls `IDXGISwapChain::Present`, and reclaims completed resources.

### Resource Reclamation

`ProcessCompletedWork` queries the render fence's completed value and forwards it to the upload ring and the device-level resource manager so they can drain their retirement queues.

## Copy Queue

`CCopyQueue` owns an independent D3D12 COPY command queue with its own fence, command allocator pool, and upload ring. Buffer and texture upload requests accumulate in pending lists until `FlushIfPending` records them all into a single command list, executes it, and signals the copy fence.

### Which Upload Ring Is Used

Two upload rings exist: one on the copy queue and one on the render queue. The routing is:

- **Mesh vertex data and texture regions** are staged through the **copy queue's** upload ring and transferred with `CopyBufferRegion` / `CopyTextureRegion` on the COPY command queue. This path is used by `CDevice12::AllocateStructuredBuffer`, `CDevice12::CreateMeshData`, and `CDevice12::UploadTextureRegion`.
- **Per-frame and per-object constant buffers** are written into the **render queue's** upload ring, which is on the UPLOAD heap and directly GPU-readable. The GPU virtual address is bound as a root CBV with no copy step.

The distinction matters because copy-queue uploads require a cross-queue fence wait before the render queue can consume them, while render-queue upload ring data is available immediately on the same queue.

### Pending Copy Operations

Two flavors of pending copy exist:

- **Buffer copies** carry source/destination resource pointers, offsets, and size.
- **Texture copies** carry a placed subresource footprint, destination subresource index, and region coordinates.

Source resources are held via `CComPtr` to keep the staging memory alive if the upload ring grows and replaces its backing resource between enqueue and flush.

### Cross-Queue Synchronization

`FlushIfPending` returns a `FenceToken`. The render queue obtains this token from `CDevice12::EnsureUploadsRetired` and issues `ID3D12CommandQueue::Wait` against the copy fence before consuming the uploaded data. No barriers are needed inside the copy command list because buffers have no layout and texture destinations must already be in COMMON.

For textures, the COMMON layout requirement is satisfied by the initial layout assigned at surface creation: default-heap surfaces start in COMMON. If a texture has been used in a render pass and transitioned away from COMMON, the caller must transition it back before enqueueing a copy. In practice, glyph atlas uploads are the primary texture copy path, and the atlas surface is created on the default heap and kept in COMMON until its first use in a render pass.

### Staging Retention

Each flush records the set of staging resources it referenced. These are released only after the copy fence reaches the signaled value, ensuring the GPU has finished reading from the upload ring's backing resource even if the ring has since grown to a new allocation.

### Shutdown

`Shutdown` drains the GPU, unregisters the timeline from the resource manager, and releases all D3D12 objects. It is called from the `CDevice12` destructor before the resource manager shuts down.

## Upload Ring

`CUploadRing` is a linear ring buffer backed by a single committed UPLOAD-heap resource that is persistently mapped for both CPU write and GPU read.

### Allocation

`AllocateFromRing` pads the write offset to the requested alignment, checks whether enough free space is available, and if not attempts to reclaim completed submissions or grow the ring. The default alignment is `D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT` (512 bytes), which satisfies both constant-buffer and texture-placement alignment requirements. The returned `HostWriteAllocation` provides the mapped CPU pointer, the GPU virtual address, and the raw `ID3D12Resource*` with offset for use as a copy source.

### Frame Markers

`MarkSubmissionEnd` records a `{fenceValue, writeOffset}` pair. `Reclaim` advances the read pointer over every marker whose fence value has completed, freeing the ring space consumed before that marker.

### Growth

When the ring cannot satisfy an allocation even after reclaiming, it grows geometrically by doubling. The old backing resource is retired into a deque and held alive until the next `MarkSubmissionEnd` tags it with a fence value and the fence completes. This ensures that outstanding GPU reads from the previous backing resource are not disrupted.

### Scope

Each upload ring is private to exactly one queue, so its fence-value markers are bare `UINT64` values on that queue's fence. This is simpler than the device-level `FenceToken` scheme, which must disambiguate across queues.

## Command Allocator Pool

`CCommandAllocatorPool` recycles D3D12 command allocators using a fence-driven multimap keyed by submitted fence value.

`SwapAllocator` returns the caller's current allocator to the pool tagged with the just-signaled fence value, then either reuses the oldest completed allocator or creates a new one. The multimap allows multiple allocators to share the same fence value.

## Swap Chain

`CSwapChain12` wraps an `IDXGISwapChain4` created with the `FLIP_SEQUENTIAL` swap effect. At construction it queries tearing support via `IDXGIFactory5::CheckFeatureSupport` and, when available, sets `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` for variable refresh rate displays.

Frame pacing uses a DXGI frame-latency waitable object. `WaitForFrameLatency` blocks until the compositor is ready for the next present. The back buffer surface is re-associated on each present to track DXGI's buffer rotation.

The swap chain supports HDR back-buffer formats such as `R16G16B16A16_Float`.

## Materials

`CMaterial12` is a CPU-side property bag holding PBR parameters and texture bindings. It stores:

- **Base color factor** (RGBA multiplier for albedo).
- **Emissive factor** (RGB multiplier for emissive).
- **Roughness / metallic / AO factor** (packed as R/G/B).
- **Textures** by role: albedo, normal, roughness, metallic, ambient occlusion, emissive.

During mesh drawing the render queue reads these properties, builds material-flag bits, and uploads per-object constants that tell the uber-shader which texture branches to enable.

### Material-to-Mesh Association

Materials are attached to mesh data at import time. `CDevice12::CreateMeshData` accepts a `MeshDataDesc` that includes per-material-group data. For FBX imports, `CanvasFbx` creates a material for each FBX material, calls `SetTexture` and factor setters, then associates the material with the corresponding `CMeshData12::GroupResources` entry. At draw time the render queue calls `pMeshData->GetMaterial(groupIndex)` to retrieve the material for each draw call.

### Adding a New PBR Parameter

Adding a parameter touches several files across the CPU/GPU boundary:

1. **`HlslTypes.h`** -- add the new field to `HlslPerObjectConstants` and, if needed, a new `MATERIAL_FLAG_*` bit.
2. **`Material12.h/cpp`** -- add a getter/setter pair and backing storage for the new parameter.
3. **`CanvasGfx.h`** -- add the getter/setter to the `XGfxMaterial` interface if the parameter should be public.
4. **`RenderQueue12.cpp`** (`DrawMesh`) -- read the new parameter from the material, set the flag bit, and populate the constant buffer field.
5. **`PSPrimary.hlsl`** -- consume the new field or texture in the pixel shader.

If the parameter adds a new texture role, also update the `SupportedRole` enum in `Material12.h` and allocate an additional SRV slot in the descriptor table.

## Mesh Data

`CMeshData12` organizes GPU vertex buffers by material group. Each group can have:

- **Position** vertex buffer (required).
- **Normal** vertex buffer.
- **UV0** vertex buffer (optional).
- **Tangent** vertex buffer (optional).
- An associated `XGfxMaterial`.

Vertex data is uploaded through the copy queue's upload ring and stored as device-local buffers. The render queue binds these as structured buffers rather than traditional input-assembled vertex buffers; the vertex shader fetches elements by `SV_VertexID`.

## Shader Pipeline

Shaders are compiled offline from HLSL source to CSO files using DXC. CMake custom commands handle compilation with Shader Model 6.0 profiles. Compiled objects are staged to the runtime directory and loaded at PSO creation time.

### Shared C++/HLSL Types

`HlslTypes.h` is included by both C++ and HLSL code. It defines the constant buffer layouts that cross the CPU/GPU boundary:

**HlslPerFrameConstants** (register b0): view-projection matrix, camera world position and basis vectors (for fullscreen view-ray reconstruction), light count, exposure multiplier, shadow atlas global parameters (`ShadowAtlasSize`, `ShadowPcfTexelStep`), Forward+ tile-grid parameters (`LightTileCountX`, `LightTileCountY`, `LightTileSizePixels`), and scene background parameters (solid color, skybox cubemap flags and blend factor, orientation quaternion, intensity, stars parameters, procedural sun disc parameters, moon billboard parameters). The per-frame light table itself is **not** in this CB; it lives in a separate `StructuredBuffer<HlslLight>` bound at `t8` and sized to `LightCount`. The tile-binning indirection lives in two more `StructuredBuffer<uint>`s at `t9` / `t10`.

**HlslPerObjectConstants** (register b1): world transform, inverse-transpose for normals, base color / emissive / roughness-metallic-AO factors, and material flag bits.

**Material flags** are per-draw bits that gate conditional branches in the uber-shader:

| Flag | Bit | Meaning |
|---|---|---|
| `MATERIAL_FLAG_HAS_UV` | 0 | UV0 stream present |
| `MATERIAL_FLAG_HAS_TANGENT` | 1 | Tangent stream present |
| `MATERIAL_FLAG_HAS_ALBEDO_TEX` | 2 | Albedo texture bound |
| `MATERIAL_FLAG_HAS_NORMAL_TEX` | 3 | Normal map bound |
| `MATERIAL_FLAG_HAS_EMISSIVE_TEX` | 4 | Emissive texture bound |
| `MATERIAL_FLAG_HAS_ROUGH_TEX` | 5 | Roughness texture bound |
| `MATERIAL_FLAG_HAS_METAL_TEX` | 6 | Metallic texture bound |
| `MATERIAL_FLAG_HAS_AO_TEX` | 7 | AO texture bound |

### Geometry Shaders

**VSPrimary** transforms vertices from structured buffers. Positions are multiplied by the world matrix and then by the view-projection matrix using the row-vector convention (`v * M`). Normals are transformed by the inverse-transpose. UV and tangent streams are conditionally fetched based on material flags.

**PSPrimary** writes to the five G-buffer targets. When a normal map is present and tangents are available, it constructs a TBN matrix and transforms the sampled normal to world space. PBR parameters are conditionally sampled and multiplied by their factors. When a flag is clear the corresponding texture sample is skipped and only the factor value is used.

### Composition Shaders

**VSFullscreen** generates a fullscreen triangle from `SV_VertexID` with no vertex buffer. Three vertices at `(0,0)`, `(2,0)`, `(0,2)` in UV space map to a triangle that covers the entire screen.

**PSComposite** reads the G-buffer surfaces and performs deferred lighting. It decodes normals from [0,1] back to [-1,1], loops over the active lights, and accumulates contributions based on light type. Point and spot lights use attenuation and cull-distance values pre-computed on the CPU. The result is scaled by the exposure multiplier.

### Text Shaders

**VSText** expands per-glyph instance data (`HlslGlyphInstance`: offset, size, atlas UVs) into screen-aligned quads using `SV_VertexID`. Each glyph is 6 vertices; `vertexId / 6` selects the glyph and `vertexId % 6` selects the quad corner. Text color is a per-draw constant from the CBV, not per-vertex data. **PSText** samples the SDF glyph atlas and uses a smoothstep threshold to produce anti-aliased glyph coverage with alpha blending.

The SDF atlas stores distance values in R8_UNorm format where 0 means far outside the glyph, 0.5 is the edge, and 1 is far inside. The pixel shader maps this to a signed distance in [-1, +1], then computes a 1-pixel-wide anti-aliased edge using `fwidth(signedDist)` as the smoothstep range. Because `fwidth` measures the screen-space rate of change, the transition width adapts automatically to the glyph's rendered size, keeping edges sharp at any scale without manual tuning.

### Rect Shaders

**VSRect** derives a screen-aligned quad entirely from `SV_VertexID` and per-draw constants (`RectSize`, `FillColor`). No vertex buffer is bound. **PSRect** outputs the interpolated fill color directly with alpha blending.

## Text Rendering Pipeline

Text rendering spans CanvasText, CanvasCore, and CanvasGfx12.

**CanvasText** parses TrueType font files, rasterizes signed-distance-field glyphs via `SDFGenerator`, and manages the glyph atlas via `CGlyphCache`. The glyph cache uses `RectanglePacker` to bin-pack SDF glyphs into a texture atlas. When a text element references glyphs not yet in the atlas, they are rasterized, packed, and queued for GPU upload.

**CanvasCore** provides `CTextLayout` for standalone text measurement and layout, and `CFont` which wraps the CanvasText `CTrueTypeFont` behind the `XFont` GEM interface.

**CanvasGfx12** owns the glyph cache instance (`CGlyphCache` is a member of `CDevice12`) and the concrete UI element implementations. `CUITextElement12` generates a compact `HlslGlyphInstance` array (32 bytes per glyph) during `Update()`, which the render queue uploads to a structured buffer and the vertex shader expands to quads on the GPU. `CUIRectElement12` carries only size and color — geometry is derived entirely on the GPU from per-draw constants.

UI elements are created exclusively via `XGfxDevice::CreateTextElement` and `XGfxDevice::CreateRectElement`. Each element has a local 2D offset relative to its parent `XUIGraphNode`. The glyph atlas surface is lazily created by `CDevice12::GetGlyphAtlasSurface`. Pending glyph uploads are flushed during `EndFrame` before UI drawing begins.

## Scene and UI Integration

CanvasCore owns the scene graph and UI graph; CanvasGfx12 consumes them during rendering.

### Scene Graph

`XScene` is a hierarchical transform graph. Each `XSceneGraphNode` carries a local rotation (quaternion), translation, and scale. Global transforms are computed lazily with dirty-flag propagation, avoiding redundant matrix recomputation when only a few nodes change.

During `Update`, the scene graph marks dirty transforms and propagates them down the hierarchy. `SubmitRenderables` then traverses the graph and calls `SubmitForRender` on the render queue for each node that carries a bound element. Lights are extracted and routed to `SubmitLight`. Mesh instances are queued for drawing in `EndFrame`.

### UI Graph

`XUIGraph` is a 2D overlay graph with position inheritance and dirty-tracked update. Each `XUIGraphNode` can have one or more bound `XUIElement` instances — text elements, rect elements, or a mix. Each element carries a signed 2D offset relative to its parent node.

Text elements (`CUITextElement12`) regenerate glyph instance buffers via `Update()` when dirty. The render queue allocates a structured buffer and uploads the glyph data at draw time. Rect elements (`CUIRectElement12`) carry size and color properties that are passed directly to the GPU as per-draw constants, requiring no buffer. Both element types are created via `XGfxDevice::CreateTextElement` and `XGfxDevice::CreateRectElement`.

The render queue processes UI nodes after the composition pass, drawing them with alpha blending over the final back buffer.

## Source Layout

| Path | Description |
|---|---|
| `Lib/Device12.*` | Device wrapper and factory |
| `Lib/ResourceAllocator.*` | Tiered placed/committed allocator with buddy sub-allocators |
| `Lib/ResourceManager.*` | Fence timelines, buffer pool, deferred release |
| `Lib/Buffer12.*` | Buffer resource wrapper |
| `Lib/Surface12.*` | Texture resource wrapper with layout tracking |
| `Lib/GpuTask.*` | GPU task graph and barrier resolution |
| `Lib/RenderQueue12.*` | Frame orchestration, deferred rendering, descriptor heaps |
| `Lib/CopyQueue.*` | Asynchronous upload pipeline |
| `Lib/UploadRing.*` | Per-queue linear ring buffer |
| `Lib/CommandAllocatorPool.*` | Fence-driven allocator recycling |
| `Lib/SwapChain12.*` | DXGI swap chain and frame pacing |
| `Lib/Material12.*` | PBR material property bag |
| `Lib/MeshData12.*` | Per-material-group vertex buffers |
| `Lib/UITextElement12.*` | SDF text element (glyph caching + instance generation) |
| `Lib/UIRectElement12.*` | Filled-rectangle element (GPU-derived geometry) |
| `Lib/CanvasGfx12.*` | GEM interface helpers and plugin glue |
| `D3D12ResourceUtils/` | Resource base classes and subresource layout tracking |

### D3D12ResourceUtils

This static library provides the base classes used throughout the backend:

- **`CResource`** -- wraps an `ID3D12Resource` COM pointer, caches its `D3D12_RESOURCE_DESC`, and computes the subresource count.
- **`CTextureResource`** -- extends `CResource` with a `SubresourceLayout` that tracks the committed barrier layout per subresource for Enhanced Barriers.
- **`SubresourceLayout`** -- the uniform/per-subresource layout tracker described in the Surfaces section above.
- **`SetD3D12DebugName`** -- helper for tagging resources with debug names.
- Utility functions for subresource index computation and planar format detection.

## Testing

Unit tests live in `src/CanvasUnitTest` and use GoogleTest. Tests relevant to CanvasGfx12:

| Test File | Coverage |
|---|---|
| `GpuTaskGraphTest.cpp` | Task creation, dependency validation, barrier resolution, fixup barrier computation, inherited state table merging |
| `D3D12ResourceUtilsTest.cpp` | Subresource count computation, layout tracking, planar format handling |
| `CanvasInterfacesTest.cpp` | GEM interface maps and QueryInterface behavior |

Run the tests with:

```
cd build
ctest --build-config Release --output-on-failure
```

## Debugging

### PIX and Validation Layer

In debug builds, CDevice12 registers a message callback with `ID3D12InfoQueue1` that routes validation-layer diagnostics through the Canvas logger. All resources are tagged with descriptive names, making them identifiable in PIX captures and validation output.

### Diagnostic Workflow

When investigating a rendering issue:

1. **Enable the D3D12 debug layer** by building in Debug configuration. Validation messages appear in the Canvas log output.
2. **Capture a PIX frame** to inspect the command list, resource states, and descriptor contents. Named resources and task names make the capture readable.
3. **Check `GetLastError`** on the task graph after `InsertTask` or `AddDependency` to see if dependency validation caught a conflict.
4. **Inspect the G-buffer** in PIX by examining individual render targets after the geometry pass. Missing geometry often points to incorrect material flags or missing vertex stream declarations.
5. **Verify upload completion** by confirming that `EnsureUploadsRetired` returns a valid fence token and that the render queue waits on it before the first draw.

## Future Directions

CanvasGfx12 is under active development. The architecture is intended to support growth in areas such as:

- Compute shader passes and UAV-driven workflows.
- Cascaded shadow maps for directional lights and shadow support for point / spot lights.
- Full PBR lighting in the composition shader (roughness, metallic, AO, emissive G-buffer targets are already populated by the geometry pass but not yet consumed).
- Multi-threaded command recording across the task graph.
- Additional rendering techniques beyond the current deferred pipeline.
- Alternative graphics backends leveraging the same CanvasCore interfaces.

None of these are committed plans. They represent directions the current design leaves room for.
