//================================================================================================
// GpuTask.h - GPU-centric Task System
//
// GPU tasks are the fundamental unit of GPU work in Canvas. Each task represents a discrete
// GPU operation (similar to a render pass) with declared resource usage. Tasks are created
// in forward execution order into a shared command list. The task graph handles:
//
//   1. Incremental barrier resolution using D3D12 Enhanced Barriers
//   2. Per-resource layout, sync, and access state tracking across tasks
//   3. Final layout computation for updating device committed state
//
// USAGE PATTERN:
//   CGpuTaskGraph graph;
//   graph.SetInitialLayout(pShadowMap, committedLayout);
//
//   // Shadow pass
//   auto& shadowPass = graph.CreateTask("ShadowMap");
//   graph.DeclareTextureUsage(shadowPass, pShadowMap,
//       D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
//       D3D12_BARRIER_SYNC_DEPTH_STENCIL,
//       D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
//   auto barriers = graph.PrepareTask(shadowPass);
//   EmitBarriers(barriers);           // caller emits into command list
//   RecordShadowMapCommands(pCL);     // caller records directly
//
//   // Main pass — reads shadow map written above
//   auto& mainPass = graph.CreateTask("MainPass");
//   graph.DeclareTextureUsage(mainPass, pShadowMap,
//       D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
//       D3D12_BARRIER_SYNC_PIXEL_SHADING,
//       D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
//   barriers = graph.PrepareTask(mainPass);
//   EmitBarriers(barriers);
//   RecordMainPassCommands(pCL);
//
//   // Finalize — update committed state
//   graph.ComputeFinalLayouts();
//   UpdateCommittedState(graph.GetFinalLayouts());
//   graph.Reset();
//
//================================================================================================

#pragma once

#include <cstdint>
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
// A single GPU task - represents a discrete GPU operation (render pass equivalent)
//
// Each task is a sync scope: it declares which resources it uses, what layouts they need,
// and which pipeline stages / access types are involved. The accumulated SyncScope and
// AccessScope masks represent the total set of GPU operations performed by this task,
// enabling precise SyncBefore/AccessBefore values on barriers.
//
// Tasks are purely declarative — they describe resource usage, not command recording.
// Commands are recorded directly into the command list by the caller after PrepareTask()
// emits any required barriers.
struct CGpuTask
{
    std::string Name;
    std::vector<GpuTextureUsage> TextureUsages;
    std::vector<GpuBufferUsage> BufferUsages;

    // Accumulated scope: union of all declared resource usage sync/access bits.
    // Represents the total set of GPU pipeline stages and access types used by this task.
    D3D12_BARRIER_SYNC SyncScope = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_ACCESS AccessScope = D3D12_BARRIER_ACCESS_NO_ACCESS;
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
// Collects GPU tasks and resolves barriers incrementally. Tasks are assumed to be created
// in forward execution order. When PrepareTask() is called after declaring resource usage,
// barriers are resolved immediately against all prior tasks — no deferred compilation needed.
//
// Lifecycle:
//   1. SetInitialLayout() for resources (from device committed state)
//   2. For each operation:
//      a. CreateTask() + DeclareTextureUsage() / DeclareBufferUsage()
//      b. PrepareTask() — resolves barriers, returns them for immediate emission
//      c. Caller records commands directly into the command list
//   3. After all tasks: GetFinalLayouts() to update device committed state
//   4. Reset() to reuse the graph for the next frame
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

    // Add an explicit dependency (task depends on dependency)
    void AddExplicitDependency(CGpuTask& task, CGpuTask& dependency);

    //---------------------------------------------------------------------------------------------
    // Barrier Resolution
    //---------------------------------------------------------------------------------------------

    // Set the initial layout for a resource before this graph executes.
    // Must be called for all resources used by tasks, typically from device committed state.
    void SetInitialLayout(ID3D12Resource* pResource, D3D12_BARRIER_LAYOUT layout, UINT subresource = 0xFFFFFFFF);

    // Prepare a task for execution: computes its sync/access scope, and resolves barriers
    // against all prior tasks. Returns barriers to emit before recording this task's commands.
    // The returned reference is valid until the next PrepareTask call or Reset.
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

    // Query the current tracked layout for a resource (reflects all PrepareTask calls so far).
    std::optional<D3D12_BARRIER_LAYOUT> GetCurrentLayout(ID3D12Resource* pResource, UINT subresource = 0xFFFFFFFF) const;

    // Get number of tasks
    uint32_t GetTaskCount() const;

    //---------------------------------------------------------------------------------------------
    // Lifecycle
    //---------------------------------------------------------------------------------------------

    // Reset the graph for reuse (clears all tasks and state)
    void Reset();

private:
    // Task pool — grow-only, reused across frames via m_TaskCount
    std::vector<CGpuTask> m_Tasks;
    uint32_t m_TaskCount = 0;

    // Initial resource layouts (from device committed state)
    std::unordered_map<ID3D12Resource*, GpuTaskGraphLayoutState> m_InitialLayouts;

    // Final resource layouts after graph execution
    std::unordered_map<ID3D12Resource*, GpuTaskGraphLayoutState> m_FinalLayouts;

    // Per-resource barrier resolution state (accumulated sync/access since last barrier)
    struct ResourceBarrierState
    {
        GpuTaskGraphLayoutState Layouts;
        D3D12_BARRIER_SYNC PendingSync = D3D12_BARRIER_SYNC_NONE;
        D3D12_BARRIER_ACCESS PendingAccess = D3D12_BARRIER_ACCESS_NO_ACCESS;
    };
    std::unordered_map<ID3D12Resource*, ResourceBarrierState> m_ResourceState;
    bool m_ResourceStateInitialized = false;

    // Scratch buffer for PrepareTask results (avoids per-call allocation)
    TaskBarriers m_ScratchBarriers;

    void EnsureResourceStateInitialized();
};

} // namespace Canvas
