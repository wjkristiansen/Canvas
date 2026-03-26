//================================================================================================
// GpuTask.h - GPU Task Graph with Explicit Dependencies
//
// A task graph manages GPU synchronization within a single ExecuteCommandLists (ECL) scope.
// Tasks form a directed acyclic graph (DAG): each task declares which resources it uses and
// which prior tasks it depends on. The graph resolves D3D12 Enhanced Barriers based on
// resource usage transitions.
//
// Key properties:
//   - Tasks are purely declarative — they describe resource usage, not commands
//   - Tasks CAN be recorded into separate command lists, provided the caller executes
//     them in task creation order within the same ECL call
//   - Dependencies must reference tasks already created in the same graph
//   - Cross-ECL layout fixups are the caller's responsibility, using GetInitialLayouts()
//     and GetFinalLayouts() to bridge committed state between execution scopes
//
// USAGE:
//   CGpuTaskGraph graph;
//   graph.SetInitialLayout(pShadowMap, committedLayout);
//
//   // Shadow pass — writes shadow map
//   auto& shadowPass = graph.CreateTask("ShadowMap");
//   graph.DeclareTextureUsage(shadowPass, pShadowMap,
//       D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
//       D3D12_BARRIER_SYNC_DEPTH_STENCIL,
//       D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
//   const auto& barriers = graph.PrepareTask(shadowPass);
//   EmitBarriers(pCL, barriers);
//   RecordShadowPass(pCL);
//
//   // Main pass — reads shadow map, depends on shadow pass
//   auto& mainPass = graph.CreateTask("MainPass");
//   graph.AddDependency(mainPass, shadowPass);
//   graph.DeclareTextureUsage(mainPass, pShadowMap,
//       D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
//       D3D12_BARRIER_SYNC_PIXEL_SHADING,
//       D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
//   const auto& barriers2 = graph.PrepareTask(mainPass);
//   EmitBarriers(pCL, barriers2);
//   RecordMainPass(pCL);
//
//   // Finalize — update committed state
//   graph.ComputeFinalLayouts();
//   UpdateCommittedState(graph.GetFinalLayouts());
//   graph.Reset();
//
//================================================================================================

#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

// Forward declarations for D3D12 types (included via pch.h in .cpp)
struct ID3D12Resource;

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Texture resource usage declared by a GPU task
struct GpuTextureUsage
{
    ID3D12Resource* pResource = nullptr;
    D3D12_BARRIER_LAYOUT RequiredLayout = D3D12_BARRIER_LAYOUT_COMMON;
    D3D12_BARRIER_SYNC Sync = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_ACCESS Access = D3D12_BARRIER_ACCESS_NO_ACCESS;
    UINT Subresources = 0xFFFFFFFF;  // All subresources by default
    D3D12_TEXTURE_BARRIER_FLAGS Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

    bool IsWrite() const
    {
        return (Access & (D3D12_BARRIER_ACCESS_RENDER_TARGET |
                          D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE |
                          D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
                          D3D12_BARRIER_ACCESS_COPY_DEST)) != 0;
    }

    bool IsRead() const
    {
        // ACCESS_COMMON (0) is a wildcard — treat as read for barrier resolution
        if (Access == D3D12_BARRIER_ACCESS_COMMON)
            return true;
        return (Access & (D3D12_BARRIER_ACCESS_SHADER_RESOURCE |
                          D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ |
                          D3D12_BARRIER_ACCESS_COPY_SOURCE |
                          D3D12_BARRIER_ACCESS_RESOLVE_SOURCE |
                          D3D12_BARRIER_ACCESS_INDEX_BUFFER |
                          D3D12_BARRIER_ACCESS_VERTEX_BUFFER |
                          D3D12_BARRIER_ACCESS_CONSTANT_BUFFER)) != 0;
    }
};

//------------------------------------------------------------------------------------------------
// Buffer resource usage declared by a GPU task
struct GpuBufferUsage
{
    ID3D12Resource* pResource = nullptr;
    D3D12_BARRIER_SYNC Sync = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_ACCESS Access = D3D12_BARRIER_ACCESS_NO_ACCESS;
    UINT64 Offset = 0;
    UINT64 Size = UINT64_MAX;  // Whole resource by default

    bool IsWrite() const
    {
        return (Access & (D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
                          D3D12_BARRIER_ACCESS_COPY_DEST)) != 0;
    }

    bool IsRead() const
    {
        // ACCESS_COMMON (0) is a wildcard — treat as read for barrier resolution
        if (Access == D3D12_BARRIER_ACCESS_COMMON)
            return true;
        return (Access & (D3D12_BARRIER_ACCESS_SHADER_RESOURCE |
                          D3D12_BARRIER_ACCESS_COPY_SOURCE |
                          D3D12_BARRIER_ACCESS_INDEX_BUFFER |
                          D3D12_BARRIER_ACCESS_VERTEX_BUFFER |
                          D3D12_BARRIER_ACCESS_CONSTANT_BUFFER)) != 0;
    }
};

//------------------------------------------------------------------------------------------------
// Inherited resource state entry — tracks the last known sync/access/layout for a
// specific subresource through a task's dependency chain. Keyed by (pResource, Subresource).
// Subresource 0xFFFFFFFF means "all subresources not otherwise specified" (uniform default).
struct ResourceStateEntry
{
    ID3D12Resource* pResource = nullptr;
    UINT Subresource = 0xFFFFFFFF;  // specific or 0xFFFFFFFF for uniform/all
    D3D12_BARRIER_SYNC Sync = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_ACCESS Access = D3D12_BARRIER_ACCESS_NO_ACCESS;
    D3D12_BARRIER_LAYOUT Layout = D3D12_BARRIER_LAYOUT_COMMON;  // textures only; ignored for buffers
    bool Inherited = true;  // true = inherited from dependency chain, false = declared by owning task
};

//------------------------------------------------------------------------------------------------
// A single GPU task — a discrete GPU operation with declared resource usage and dependencies
//
// Tasks are sync scopes: they declare which resources they use, what layouts/sync/access
// are required, and which prior tasks they depend on. The task graph uses this to resolve
// barriers. Commands are recorded by the caller, not by the task.
struct CGpuTask
{
    std::string Name;
    uint32_t Index = 0;  // Creation order within graph
    std::vector<const CGpuTask*> Dependencies;
    std::vector<GpuTextureUsage> TextureUsages;
    std::vector<GpuBufferUsage> BufferUsages;

    // Inherited resource state table — populated by PrepareTask().
    // Contains the state of every resource reachable through this task's dependency
    // chain, including this task's own usages. Used by downstream tasks for O(n) lookup.
    std::vector<ResourceStateEntry> ResourceStates;
};

//------------------------------------------------------------------------------------------------
// Resolved barrier to emit before a task executes
struct ResolvedTextureBarrier
{
    ID3D12Resource* pResource;
    D3D12_BARRIER_SYNC SyncBefore;
    D3D12_BARRIER_SYNC SyncAfter;
    D3D12_BARRIER_ACCESS AccessBefore;
    D3D12_BARRIER_ACCESS AccessAfter;
    D3D12_BARRIER_LAYOUT LayoutBefore;
    D3D12_BARRIER_LAYOUT LayoutAfter;
    UINT Subresources;
    D3D12_TEXTURE_BARRIER_FLAGS Flags;
};

struct ResolvedBufferBarrier
{
    ID3D12Resource* pResource;
    D3D12_BARRIER_SYNC SyncBefore;
    D3D12_BARRIER_SYNC SyncAfter;
    D3D12_BARRIER_ACCESS AccessBefore;
    D3D12_BARRIER_ACCESS AccessAfter;
    UINT64 Offset;
    UINT64 Size;
};

//------------------------------------------------------------------------------------------------
// Barriers to insert before a specific task executes
struct TaskBarriers
{
    std::vector<ResolvedTextureBarrier> TextureBarriers;
    std::vector<ResolvedBufferBarrier> BufferBarriers;
};

//------------------------------------------------------------------------------------------------
// Per-resource layout state tracking for initial/final layouts
// Used to interface with the device's committed layout tracking
struct GpuTaskGraphLayoutState
{
    std::optional<D3D12_BARRIER_LAYOUT> UniformLayout;
    std::unordered_map<UINT, D3D12_BARRIER_LAYOUT> PerSubresourceLayouts;

    D3D12_BARRIER_LAYOUT GetLayout(UINT subresource) const
    {
        if (UniformLayout.has_value())
            return UniformLayout.value();
        auto it = PerSubresourceLayouts.find(subresource);
        return (it != PerSubresourceLayouts.end()) ? it->second : D3D12_BARRIER_LAYOUT_COMMON;
    }

    void SetLayout(UINT subresource, D3D12_BARRIER_LAYOUT layout)
    {
        if (subresource == 0xFFFFFFFF)
        {
            UniformLayout = layout;
            PerSubresourceLayouts.clear();
        }
        else
        {
            UniformLayout.reset();
            PerSubresourceLayouts[subresource] = layout;
        }
    }
};

//------------------------------------------------------------------------------------------------
// GPU Task Graph
//
// Manages GPU synchronization within a single ExecuteCommandLists scope. Tasks form a DAG
// with explicit dependencies. Barriers are resolved incrementally via PrepareTask() — tasks
// must be prepared in creation order.
//
// Tasks do not record commands — the caller records directly after emitting barriers.
// Tasks CAN target separate command lists within the same ECL call.
//
// Lifecycle:
//   1. SetInitialLayout() for known resource states
//   2. For each operation:
//      a. CreateTask() + AddDependency() + DeclareTextureUsage/DeclareBufferUsage()
//      b. PrepareTask() — resolves and returns barriers for immediate emission
//      c. Caller emits barriers and records commands
//   3. ComputeFinalLayouts() → caller updates device committed state
//   4. Reset() for next frame
//
class CGpuTaskGraph
{
public:
    CGpuTaskGraph();
    ~CGpuTaskGraph();

    // Non-copyable, movable
    CGpuTaskGraph(const CGpuTaskGraph&) = delete;
    CGpuTaskGraph& operator=(const CGpuTaskGraph&) = delete;
    CGpuTaskGraph(CGpuTaskGraph&&) = default;
    CGpuTaskGraph& operator=(CGpuTaskGraph&&) = default;

    //---------------------------------------------------------------------------------------------
    // Task Creation
    //---------------------------------------------------------------------------------------------

    // Create a new GPU task. The returned reference is valid until Reset().
    CGpuTask& CreateTask(const char* name = nullptr);

    // Declare texture usage for a task
    void DeclareTextureUsage(
        CGpuTask& task,
        ID3D12Resource* pResource,
        D3D12_BARRIER_LAYOUT requiredLayout,
        D3D12_BARRIER_SYNC sync,
        D3D12_BARRIER_ACCESS access,
        UINT subresources = 0xFFFFFFFF,
        D3D12_TEXTURE_BARRIER_FLAGS flags = D3D12_TEXTURE_BARRIER_FLAG_NONE);

    // Declare buffer usage for a task
    void DeclareBufferUsage(
        CGpuTask& task,
        ID3D12Resource* pResource,
        D3D12_BARRIER_SYNC sync,
        D3D12_BARRIER_ACCESS access,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX);

    // Add a dependency: task will not execute until dependency completes.
    // Sets an error via GetLastError() if the dependency is invalid.
    void AddDependency(CGpuTask& task, const CGpuTask& dependency);

    //---------------------------------------------------------------------------------------------
    // Barrier Resolution
    //---------------------------------------------------------------------------------------------

    // Set initial layout for a resource (from committed state). Resources not set
    // use first-use semantics: the first task's required layout is assumed, and the
    // caller bridges via fixup barriers at ECL time.
    void SetInitialLayout(ID3D12Resource* pResource, D3D12_BARRIER_LAYOUT layout, UINT subresource = 0xFFFFFFFF);

    // Resolve barriers for a task based on resource usage and graph state.
    // Must be called in task creation order. Returns barriers to emit before
    // recording this task's commands. Valid until the next PrepareTask() or Reset().
    // Sets an error via GetLastError() if dependency conflicts are detected.
    const TaskBarriers& PrepareTask(CGpuTask& task);

    //---------------------------------------------------------------------------------------------
    // Queries
    //---------------------------------------------------------------------------------------------

    // Compute final layouts from all tasks created so far.
    // Call after all tasks have been prepared.
    void ComputeFinalLayouts();

    // Get the final layout of each resource after the graph completes.
    // Use this to update device committed state.
    const std::unordered_map<ID3D12Resource*, GpuTaskGraphLayoutState>& GetFinalLayouts() const;

    // Get the initial layouts that this graph was built against.
    // Use this at ECL time to compute fixup barriers.
    const std::unordered_map<ID3D12Resource*, GpuTaskGraphLayoutState>& GetInitialLayouts() const;

    // Get number of tasks
    uint32_t GetTaskCount() const;

    // Last error message from AddDependency or PrepareTask (empty if no error)
    const std::string& GetLastError() const;

    //---------------------------------------------------------------------------------------------
    // Lifecycle
    //---------------------------------------------------------------------------------------------

    // Reset the graph for reuse (clears logical state, preserves pool capacity)
    void Reset();

    // Release all pooled memory (scene transitions, shutdown). Next use rebuilds pools.
    void ReleaseMemory();

private:
    // Task pool — grow-only deque (references remain valid across push_back).
    // Reused across frames via m_TaskCount.
    std::deque<CGpuTask> m_Tasks;
    uint32_t m_TaskCount = 0;

    // Initial resource layouts (from device committed state)
    std::unordered_map<ID3D12Resource*, GpuTaskGraphLayoutState> m_InitialLayouts;

    // Final resource layouts after graph execution
    std::unordered_map<ID3D12Resource*, GpuTaskGraphLayoutState> m_FinalLayouts;

    // Scratch buffer for PrepareTask results (avoids per-call allocation)
    TaskBarriers m_ScratchBarriers;

    // Last error from validation
    std::string m_LastError;

    // Look up a resource+subresource in a task's inherited resource state table.
    // Tries exact (pResource, subresource) match first, falls back to uniform entry (0xFFFFFFFF).
    // Returns nullptr if the resource is not in the table.
    static const ResourceStateEntry* FindResourceState(
        const std::vector<ResourceStateEntry>& table, ID3D12Resource* pResource, UINT subresource = 0xFFFFFFFF);

    // Merge a resource state entry into a table (overwrite if exists, append if not).
    static void MergeResourceState(
        std::vector<ResourceStateEntry>& table, const ResourceStateEntry& entry);

    // Union a resource state entry into a table (OR sync/access if exists).
    static void UnionResourceState(
        std::vector<ResourceStateEntry>& table, const ResourceStateEntry& entry);

    // Validate that direct dependencies don't leave any resource in conflicting states.
    // Allocates a temporary map. Sets m_LastError if conflicts detected.
    void ValidateDependencyConflicts(const CGpuTask& task);
};

//------------------------------------------------------------------------------------------------
// Predefined usage helpers — correct sync/access/layout for common patterns.
// Prefer DIRECT_QUEUE-specific layouts for optimal performance on direct queues.
//------------------------------------------------------------------------------------------------

// Texture helpers
inline GpuTextureUsage TextureUsageRenderTarget(ID3D12Resource* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET, sub };
}
inline GpuTextureUsage TextureUsageDepthStencilWrite(ID3D12Resource* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE, sub };
}
inline GpuTextureUsage TextureUsageDepthStencilRead(ID3D12Resource* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ, D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ, sub };
}
inline GpuTextureUsage TextureUsagePixelShaderResource(ID3D12Resource* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, sub };
}
inline GpuTextureUsage TextureUsageNonPixelShaderResource(ID3D12Resource* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE, D3D12_BARRIER_SYNC_NON_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, sub };
}
inline GpuTextureUsage TextureUsageAllShaderResource(ID3D12Resource* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE, D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, sub };
}
inline GpuTextureUsage TextureUsageCopySource(ID3D12Resource* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE, sub };
}
inline GpuTextureUsage TextureUsageCopyDest(ID3D12Resource* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST, sub };
}
inline GpuTextureUsage TextureUsageUnorderedAccess(ID3D12Resource* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, sub };
}
inline GpuTextureUsage TextureUsagePresent(ID3D12Resource* p) {
    return { p, D3D12_BARRIER_LAYOUT_COMMON, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS, 0xFFFFFFFF };
}

// Buffer helpers
inline GpuBufferUsage BufferUsageVertexBuffer(ID3D12Resource* p) {
    return { p, D3D12_BARRIER_SYNC_VERTEX_SHADING, D3D12_BARRIER_ACCESS_VERTEX_BUFFER };
}
inline GpuBufferUsage BufferUsageIndexBuffer(ID3D12Resource* p) {
    return { p, D3D12_BARRIER_SYNC_INDEX_INPUT, D3D12_BARRIER_ACCESS_INDEX_BUFFER };
}
inline GpuBufferUsage BufferUsageConstantBuffer(ID3D12Resource* p) {
    return { p, D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_CONSTANT_BUFFER };
}
inline GpuBufferUsage BufferUsageCopySource(ID3D12Resource* p) {
    return { p, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE };
}
inline GpuBufferUsage BufferUsageCopyDest(ID3D12Resource* p) {
    return { p, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST };
}
inline GpuBufferUsage BufferUsageUnorderedAccess(ID3D12Resource* p) {
    return { p, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS };
}
inline GpuBufferUsage BufferUsageIndirectArgument(ID3D12Resource* p) {
    return { p, D3D12_BARRIER_SYNC_EXECUTE_INDIRECT, D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT };
}

} // namespace Canvas
