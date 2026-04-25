# CanvasGfx12

The Direct3D 12 graphics backend for Canvas. CanvasGfx12 is loaded at runtime as a DLL plugin via `XCanvas::LoadPlugin` and exposes `XGfxDevice`, from which all GPU objects are created.

This document describes the current state of the design. CanvasGfx12 is still being developed; subsystems described here may be extended, restructured, or replaced as the project evolves.

## Overview

CanvasGfx12 translates the abstract Canvas graphics interfaces into D3D12 API calls. Its major responsibilities are:

- **Resource allocation and lifecycle** across GPU heaps, buffer pools, and deferred-release queues.
- **Automatic barrier management** via a DAG-based task graph that uses D3D12 Enhanced Barriers.
- **Frame rendering** through a deferred shading pipeline with a 5-target G-buffer, fullscreen composition, and UI overlay.
- **Asynchronous uploads** on a dedicated COPY queue with cross-queue fence synchronization.

All public types are GEM interfaces. Smart pointers (`Gem::TGemPtr<T>`) manage lifetimes, and `QueryInterface` discovers capabilities at the ABI boundary.

## Plugin Entry Point

The DLL exports a `CreateCanvasPlugin` factory. CanvasCore calls this after `LoadPlugin` to obtain an `XCanvasPlugin` instance, which in turn creates `CDevice12` objects on demand. The plugin has no static registration; all wiring happens through GEM interface discovery.

## Device

`CDevice12` is the root object for the backend. It wraps an `ID3D12Device10` and owns the two major infrastructure pieces: the resource manager and the copy queue. It also serves as the factory for render queues, buffers, surfaces, materials, mesh data, and UI elements.

On initialization, CDevice12 creates the D3D12 device, queries adapter information, and in debug builds registers a validation-layer message callback that routes diagnostics through the Canvas logger.

Key factory methods:

| Method | Creates |
|---|---|
| `CreateRenderQueue` | `CRenderQueue12` |
| `CreateSurface` | `CSurface12` |
| `CreateBuffer` | `CBuffer12` |
| `CreateMeshData` | `CMeshData12` |
| `CreateMaterial` | `CMaterial12` |
| `AllocVertexBuffer` | Pooled buffer via resource manager, staged through copy queue |
| `UploadTextureRegion` | Texture upload via copy queue staging |

Memory usage types map directly to D3D12 heap types:

| Canvas Usage | D3D12 Heap Type |
|---|---|
| `DeviceLocal` | `DEFAULT` |
| `HostWrite` | `UPLOAD` |
| `HostRead` | `READBACK` |

All resources are created with `D3D12_RESOURCE_DESC1` and `D3D12_BARRIER_LAYOUT` for Enhanced Barriers compatibility. Debug builds tag every resource with a human-readable name for PIX and the validation layer.

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

The task graph reads and writes this layout state when computing fixup barriers and final layouts.

Swap chain back buffers are special-cased: the surface holds a weak pointer to its owning `CSwapChain12` so it can be re-associated when DXGI rotates the back buffer index.

## GPU Task Graph

`CGpuTaskGraph` manages GPU synchronization within a single `ExecuteCommandLists` scope. Tasks form a directed acyclic graph where each task declares which resources it uses and which prior tasks it depends on. The graph resolves D3D12 Enhanced Barriers based on resource usage transitions.

### Design Principles

- Tasks are purely declarative. They describe resource usage, not commands.
- Recording functions are invoked after barriers are resolved, ensuring atomic barrier-then-work sequences.
- Dependencies must reference tasks already created in the same graph.
- Cross-ECL layout bridging is handled by fixup barriers at dispatch time.

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

### Task Graphs

The render queue owns three independent `CGpuTaskGraph` instances:

| Graph | Purpose |
|---|---|
| **Scene** | Geometry pass, per-object draws, deferred composition |
| **UI** | Text and rectangle overlay draws |
| **Present** | Back buffer transition to COMMON for DXGI presentation |

Each graph has its own work and fixup command lists and pulls allocators from a shared `CCommandAllocatorPool`.

### Descriptor Heap Management

Four descriptor heaps are created at initialization:

| Heap | Type | Count | Visibility |
|---|---|---|---|
| CBV/SRV/UAV | Shader resource | 65,536 | Shader-visible |
| Sampler | Sampler | 1,024 | Shader-visible |
| RTV | Render target view | 64 | CPU-only |
| DSV | Depth stencil view | 64 | CPU-only |

The shader-visible heap is used as a cycling allocator: `m_NextSRVSlot` advances with each draw and wraps at 65,536. Each mesh draw allocates 13 contiguous descriptors: 2 CBVs, 9 SRVs, and 2 UAVs. RTV and DSV slots wrap at 64.

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
- Slot 0: root CBV at b0 for lighting constants.
- Slot 1: descriptor table with SRV[3] at t0-t2 reading three G-buffer surfaces.
- Static sampler s0: point clamp for exact texel fetch.
- Single render target at back-buffer format. No depth.

**Text pass**:
- Slot 0: root CBV at b0 for screen-space constants.
- Slot 1: root SRV at t0 for the text vertex buffer.
- Slot 2: descriptor table with SRV[1] at t1 for the SDF glyph atlas.
- Static sampler s0: linear clamp.
- Alpha blending: `SRC_ALPHA / INV_SRC_ALPHA`.

**Rect pass**:
- Slot 0: root CBV at b0 for screen-space constants.
- Slot 1: root SRV at t0 for the vertex buffer.
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

A D32_Float depth buffer uses reverse-Z with `GREATER_EQUAL` comparison. G-buffers are created lazily and resized when the swap chain dimensions change.

### Geometry Drawing

For each mesh instance the render queue:

1. Looks up the per-material-group vertex streams (position, normal, UV0, tangent).
2. Resolves the material and gathers its textures by role.
3. Builds material flags that enable or disable uber-shader branches.
4. Uploads per-object constants to the upload ring.
5. Allocates 13 descriptors and populates them with CBVs for the per-object constants, SRVs for vertex streams and textures, and null UAV placeholders.
6. Creates a GPU task that declares buffer and texture usages, then records the draw call.

Positions are bound as a root SRV at t0 by GPU virtual address. Remaining vertex streams and material textures are bound through the descriptor table.

### Light Submission

Lights are accumulated during scene graph traversal. Each light's attenuation and cull distance are pre-computed on the CPU and packed into an `HlslLight` struct. Up to `MAX_LIGHTS_PER_REGION` (16) lights are uploaded as part of the per-frame constant buffer.

### Composition

After all geometry tasks are inserted, the render queue transitions the G-buffer surfaces from render target to shader resource layout and inserts a composition task. This task binds the composition PSO, sets the back buffer as the single render target, and draws a fullscreen triangle that performs deferred lighting in the pixel shader.

### UI Drawing

After the scene graph, the render queue processes the UI renderable queue. Text elements are drawn with the text PSO using the SDF glyph atlas. Rectangle elements use the rect PSO with per-vertex color. Both use alpha blending onto the back buffer.

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

### Pending Copy Operations

Two flavors of pending copy exist:

- **Buffer copies** carry source/destination resource pointers, offsets, and size.
- **Texture copies** carry a placed subresource footprint, destination subresource index, and region coordinates.

Source resources are held via `CComPtr` to keep the staging memory alive if the upload ring grows and replaces its backing resource between enqueue and flush.

### Cross-Queue Synchronization

`FlushIfPending` returns a `FenceToken`. The render queue obtains this token from `CDevice12::EnsureUploadsRetired` and issues `ID3D12CommandQueue::Wait` against the copy fence before consuming the uploaded data. No barriers are needed inside the copy command list because buffers have no layout and texture destinations must already be in COMMON.

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

**HlslPerFrameConstants** (register b0): view-projection matrix, camera world position, light count, exposure multiplier, and an array of `HlslLight` structs.

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

**VSText** reads a text vertex buffer as a structured buffer and emits screen-space quads. **PSText** samples the SDF glyph atlas and uses a smoothstep threshold to produce anti-aliased glyph coverage with alpha blending.

### Rect Shaders

**VSRect** reads a rect vertex buffer and emits screen-space quads. **PSRect** outputs the per-vertex color directly with alpha blending.

## Text Rendering Pipeline

Text rendering spans CanvasText, CanvasCore, and CanvasGfx12.

**CanvasText** parses TrueType font files and rasterizes signed-distance-field glyphs via `SDFGenerator`. Each glyph is rendered at a fixed SDF resolution and stored as a single-channel distance field.

**GlyphAtlas** in CanvasCore uses `RectanglePacker` to bin-pack SDF glyphs into a texture atlas. When a text element references glyphs not yet in the atlas, they are rasterized, packed, and the atlas region is uploaded to the GPU through the device's `UploadTextureRegion` method.

**TextLayout** in CanvasCore performs line breaking, glyph placement using font metrics, and generates the vertex buffer that the text shader consumes. The vertex buffer is allocated and uploaded by the graphics device, keeping GPU resource management out of the core layer.

**FontImpl** wraps the CanvasText `Font` class behind the `XFont` GEM interface, providing metric access for ascender, descender, and units-per-em.

The glyph atlas surface is lazily created by `CDevice12::GetGlyphAtlasSurface`. Pending glyph uploads are flushed during `EndFrame` before UI drawing begins.

## Scene and UI Integration

CanvasCore owns the scene graph and UI graph; CanvasGfx12 consumes them during rendering.

### Scene Graph

`XSceneGraph` is a hierarchical transform graph. Each `XSceneGraphNode` carries a local rotation (quaternion), translation, and scale. Global transforms are computed lazily with dirty-flag propagation, avoiding redundant matrix recomputation when only a few nodes change.

During `Update`, the scene graph marks dirty transforms and propagates them down the hierarchy. `SubmitRenderables` then traverses the graph and calls `SubmitForRender` on the render queue for each node that carries a bound element. Lights are extracted and routed to `SubmitLight`. Mesh instances are queued for drawing in `EndFrame`.

### UI Graph

`XUIGraph` is a 2D overlay graph with position inheritance and dirty-tracked update. Nodes carry `XUITextElement` or `XUIRectElement` instances that generate their own vertex buffers when marked dirty. The render queue processes UI nodes after the composition pass, drawing them with alpha blending over the final back buffer.

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
| `Lib/CanvasGfx12.*` | GEM interface helpers and plugin glue |
| `D3D12ResourceUtils/` | Resource base classes and subresource layout tracking |

## Future Directions

CanvasGfx12 is under active development. The architecture is intended to support growth in areas such as:

- Compute shader passes and UAV-driven workflows.
- Shadow mapping and additional lighting techniques.
- Multi-threaded command recording across the task graph.
- Additional rendering techniques beyond the current deferred pipeline.
- Alternative graphics backends leveraging the same CanvasCore interfaces.

None of these are committed plans. They represent directions the current design leaves room for.
