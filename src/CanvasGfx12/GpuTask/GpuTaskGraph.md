# GPU Task Graph

The GPU Task Graph is a DAG where each node represents a set of GPU commands recorded into
a shared command list. Tasks can be forked and joined, with barrier resolution driven by
declared dependencies and resource usage.

## Scope

The task graph handles **resource barrier resolution** — layout transitions, sync, and access
hazards between tasks based on explicit dependencies. It does not track PSO, root signature,
descriptor heap, or other command list state. Each task function is responsible for setting
its own pipeline state.

## GpuTask Node

Each GpuTask node represents:

- A recording function (callable or lambda) that records commands into the work CL
- A set of ResourceUsage descriptors declaring layout/sync/access for each resource the task touches
- A list of dependency pointers to predecessor tasks (must already exist in the graph)

The task does **not** own a command list pointer. All tasks record into the graph's
shared work CL, which is passed to the recording function.

### Task Lifecycle

1. Caller creates a task, declares resource usages and dependencies
2. Graph resolves barriers by scanning dependency usages (forward accumulation at insertion time)
3. Graph emits resolved barriers into the work CL
4. Graph invokes the task's recording function, passing the work CL
5. Task function records its GPU commands directly

Steps 2-4 happen atomically when the task is inserted into the graph.

## GpuTaskGraph

Owns the DAG nodes and the command lists for a single ExecuteCommandLists scope.

### Owned Resources

- **Work CL** — the command list all task recording functions write into
- **Fixup CL** — a separate command list for deferred layout fixup barriers at dispatch time
- **Command allocators** for both CLs (pooled per frame)

### Dispatch

When the graph is dispatched:

1. Walk all tasks to discover the expected initial layout of each texture subresource
   (the first task to use each resource determines the expected initial layout)
2. Compare against committed device state — emit fixup barriers into the fixup CL for mismatches
3. Close both CLs
4. ExecuteCommandLists: [fixup CL, work CL]
5. Walk all tasks to compute final layouts — update committed device state

There is no implicit root task node. The fixup barrier step is internal logic within Dispatch,
not a GpuTask. Every task in the graph is a real task with a recording function.

### Final Layout Computation

After dispatch, walk all tasks in creation order. The last task to declare a usage for each
resource determines the final layout. Update committed device state accordingly.

## GpuTaskManager

A container for pooled resources shared across frames:

- Task pool (`std::deque<GpuTask>` — stable references across push_back)
- Command allocator pools (rotated per frame)
- Any scratch buffers for barrier resolution

The GpuTaskManager is owned by the render queue and provides GpuTaskGraph instances per frame.

## Barrier Resolution

### Core Rule

**A barrier is only emitted when the current task has a dependency (direct or transitive) on
a task that previously accessed the resource in a conflicting way.**

No dependency chain → no barrier. The task is deliberately accessing the resource without
requiring prior work to be visible (e.g. simultaneous access, or don't-care reads).

### Inherited Resource State Table

Each task carries a **resource state table** — a flat list of `{pResource, Sync, Access, Layout}`
entries representing the last known state of every resource reachable through the task's
dependency chain.

When a task is inserted:

1. **Single dependency**: Copy the dep's table, then overwrite entries for resources the new task uses
2. **Multiple dependencies (join)**: Merge tables from all deps — for resources present in multiple
   deps, union Sync/Access bits (debug: validate no layout conflicts via `ValidateDependencyConflicts`)
3. **No dependencies**: Start with an empty table — all resources fall through to initial/committed state

This avoids transitive graph walks at barrier resolution time. The table is built incrementally
as tasks are inserted. Lookup is O(n) over the table size (number of distinct resources in the
dependency chain), which is typically 5-15 entries for a real frame.

The table can be a pooled `std::vector<ResourceStateEntry>` sorted by resource pointer for
binary search, or a small flat map. The GpuTaskManager provides pooled storage so tables
don't allocate per-task.

### Algorithm (at task insertion)

For each resource the new task declares:

1. Look up the resource in the task's inherited resource state table
2. If found: use the entry's sync/access/layout as SyncBefore/AccessBefore/LayoutBefore
3. If not found: use initial layout (from `SetInitialLayout` or first-use semantics)
4. Determine if a barrier is needed:
   - Layout change → barrier
   - Prior state was a write (RAW) → barrier
   - Current task writes and prior state exists (WAR/WAW) → barrier
   - No prior state (ECL boundary) → barrier only if layout differs from initial
5. Emit barrier into work CL if needed
6. Update the task's resource state table with this task's declared usage

### First-Use Semantics

If a task uses a resource that has no `SetInitialLayout` and no dependency touched it:

- Assume the task's required layout as the initial layout
- Record this assumed layout for fixup barrier resolution at dispatch time

### D3D12 Barrier Bit Semantics

Reference: [D3D12 Enhanced Barriers Spec](https://github.com/microsoft/DirectX-Specs/blob/master/d3d/D3D12EnhancedBarriers.md)

The barrier resolution and state table merge logic must respect D3D12 Enhanced Barrier bit rules:

**ACCESS_NO_ACCESS (0x80000000)** is a sentinel — it means "no access declared." It must never
be OR'd with real access bits. When merging or accumulating access:
- If prior access is NO_ACCESS, assign (don't OR)
- If new access is NO_ACCESS, keep the prior value

**Aggregate sync bits** like `SYNC_ALL`, `SYNC_DRAW`, and `SYNC_COMPUTE` imply multiple explicit
sync bits. When comparing or merging sync values, these aggregates must be treated as supersets.
For barrier emission, prefer the most specific sync bits possible to avoid unnecessary pipeline stalls.

**Aggregate access bits** like `ACCESS_COMMON (0)` act as wildcards — they are compatible with
all access types. For barrier resolution, treat ACCESS_COMMON as a read for hazard detection
purposes. **Avoid using `ACCESS_COMMON` as `AccessBefore`** — it implies all write access types
and may cause unnecessary cache flushes. Use explicit write bits instead.

**`SYNC_NONE` must pair with `ACCESS_NO_ACCESS`**: A `SyncBefore` of `SYNC_NONE` means no
preceding access in this ECL scope. A `SyncAfter` of `SYNC_NONE` means no subsequent access.
Both must use `NO_ACCESS` on the corresponding side.

**Sequential barriers on the same subresource**: Any barrier following another barrier on the
same subresource in the same ECL scope must use a `SyncBefore` that fully contains the
preceding barrier's `SyncAfter` scope bits. The inherited state table naturally satisfies this
since each task's state entry replaces the prior entry for that resource.

**Prefer queue-specific layouts**: When all access to a texture occurs on a single queue type
(e.g. Direct), use queue-specific layouts like `D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE`
instead of generic `D3D12_BARRIER_LAYOUT_SHADER_RESOURCE`. This can avoid unnecessary texture
decompression on some hardware. The task graph does not enforce this — it is the caller's
responsibility to declare the optimal layout.

**Barrier-free access at ECL boundaries**: At the start of an ECL scope, buffers and textures
in COMMON layout can be accessed for certain operations without a barrier. The task graph
models this via first-use / initial-state semantics — if no dependency touched the resource
and the initial layout matches the required layout, no barrier is emitted.

## Resource Usage Declaration

Each task declares its resource usages as a list of:

### Texture Usage
- `ID3D12Resource*` pointer
- `D3D12_BARRIER_LAYOUT` — required layout
- `D3D12_BARRIER_SYNC` — pipeline stage
- `D3D12_BARRIER_ACCESS` — access type
- Subresource index (0xFFFFFFFF = all)
- `D3D12_TEXTURE_BARRIER_FLAGS`

### Buffer Usage
- `ID3D12Resource*` pointer
- `D3D12_BARRIER_SYNC` — pipeline stage
- `D3D12_BARRIER_ACCESS` — access type
- Offset / Size range

### Predefined Usage Helpers

A set of predefined usage factories are provided for common patterns to reduce boilerplate
and ensure correct sync/access/layout combinations:

**Texture helpers** (each returns a `GpuTextureUsage` with correct sync, access, and layout):
- `TextureUsageRenderTarget(pResource)` — RT write
- `TextureUsageDepthStencilWrite(pResource)` — DSV write
- `TextureUsageDepthStencilRead(pResource)` — DSV read-only
- `TextureUsagePixelShaderResource(pResource)` — SRV in pixel shader
- `TextureUsageAllShaderResource(pResource)` — SRV in all shader stages
- `TextureUsageNonPixelShaderResource(pResource)` — SRV in vertex/compute/etc
- `TextureUsageCopySource(pResource)` — copy source
- `TextureUsageCopyDest(pResource)` — copy destination
- `TextureUsageUnorderedAccess(pResource)` — UAV
- `TextureUsagePresent(pResource)` — transition to COMMON for Present (SYNC_NONE/NO_ACCESS)

**Buffer helpers** (each returns a `GpuBufferUsage` with correct sync and access):
- `BufferUsageVertexBuffer(pResource)` — vertex buffer read
- `BufferUsageIndexBuffer(pResource)` — index buffer read
- `BufferUsageConstantBuffer(pResource)` — CBV read
- `BufferUsageCopySource(pResource)` — copy source
- `BufferUsageCopyDest(pResource)` — copy destination
- `BufferUsageUnorderedAccess(pResource)` — UAV
- `BufferUsageIndirectArgument(pResource)` — indirect argument read

These helpers prefer queue-specific layouts where applicable (e.g. `DIRECT_QUEUE_SHADER_RESOURCE`
over `SHADER_RESOURCE` when compiling for a direct queue graph).

## Rules

### Task Creation Order

Tasks must be created in serialized order — dependency tasks must exist before their dependents.
Even logically parallel tasks (e.g. diamond pattern) must be created sequentially.

### Parallel Task State Invariant

Parallel tasks (tasks with no mutual dependency) must not produce conflicting resource state.
If TaskC depends on both TaskA and TaskB, any resource touched by both A and B must be left
in the same layout/sync/access regardless of A/B serialization order. Violations are detected
in debug builds via `ValidateDependencyConflicts`.

### Task Functions Execute Immediately

Task recording functions are invoked at insertion time, not deferred. They are already
"deferred" by virtue of recording into a command list — the GPU executes them later.

### Self-Dependency

A task cannot depend on itself. `AddDependency` rejects this with an error.

## Resolved Decisions

| Decision | Resolution |
|----------|------------|
| Task pool container | `std::deque` — stable references, no invalidation on growth |
| State accumulation | Inherited resource state table — forward propagation at insertion, O(1) lookup per resource |
| PSO/binding tracking | Not in scope — each task function sets its own pipeline state |
| CL ownership | GpuTaskGraph owns work CL + fixup CL |
| Error reporting | `GetLastError()` for runtime errors, `ValidateDependencyConflicts` in debug only |
| Fixup barrier timing | Deferred until dispatch — graph walks tasks to discover initial layouts |