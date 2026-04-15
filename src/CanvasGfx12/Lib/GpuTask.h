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
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

#include "CommandAllocatorPool.h"
#include "D3D12ResourceUtils.h"
#include "Buffer12.h"
#include "Surface12.h"

struct ID3D12Resource;
struct ID3D12Device;

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Texture resource usage declared by a GPU task
struct GpuTextureUsage
{
    CSurface12* pSurface = nullptr;
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
    CBuffer12* pBuffer = nullptr;
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
// Each task has a recording function that records GPU commands into the graph's work CL.
// The graph resolves barriers and invokes the recording function atomically at insertion time.
struct CGpuTask
{
    std::string Name;
    uint32_t Index = 0;  // Creation order within graph
    std::vector<const CGpuTask*> Dependencies;
    std::vector<GpuTextureUsage> TextureUsages;
    std::vector<GpuBufferUsage> BufferUsages;

    // Recording function — invoked synchronously by InsertTask after barriers are emitted.
    // Receives the graph's work CL for command recording.
    //
    // IMPORTANT: Capture only raw pointers and plain-old-data (GPU addresses, offsets, counts).
    // Do NOT capture ref-counting smart pointers (TGemPtr, GfxResourceAllocation, etc.).
    // The lambda is invoked inline by InsertTask — the caller's stack keeps objects alive.
    // Captured TGemPtrs create hidden refs that survive in the task deque after Reset(),
    // causing resource leaks.
    //
    // May be null for transition-only tasks (e.g. PresentTransition).
    std::function<void(ID3D12GraphicsCommandList*)> RecordFunc;

    // Inherited resource state table — populated by PrepareTask/InsertTask().
    std::vector<ResourceStateEntry> ResourceStates;
};

//------------------------------------------------------------------------------------------------
// Resolved barrier to emit before a task executes
struct ResolvedTextureBarrier
{
    CSurface12* pSurface;
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
    CBuffer12* pBuffer;
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
// GPU Task Graph
//
// Manages GPU synchronization within a single ExecuteCommandLists scope. Tasks form a DAG
// with explicit dependencies. Each task has a recording function that records GPU commands.
//
// The graph owns a work CL and fixup CL (set via SetCommandLists). When a task is inserted:
//   1. Barriers are resolved from the dependency-driven state table
//   2. Resolved barriers are emitted into the work CL
//   3. The task's recording function is invoked with the work CL
// These three steps happen atomically in InsertTask().
//
// At dispatch time, fixup barriers bridge committed device state to assumed initial layouts.
//
// Lifecycle:
//   1. Init(device, workAllocator, fixupAllocator) — creates owned CLs
//   2. For each operation:
//      a. CreateTask() + AddDependency() + DeclareTextureUsage/DeclareBufferUsage()
//      b. Set task.RecordFunc (optional for transition-only tasks)
//      c. InsertTask() — resolves barriers, emits, invokes recording function
//   3. Dispatch(committedLayouts) — closes work CL, fixup barriers, closes fixup CL
//   4. Caller submits [GetFixupCommandList(), GetWorkCommandList()] via ECL
//   5. Caller updates committed state from GetFinalLayouts()
//   6. Reset(workAlloc, fixupAlloc) for next frame
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
    // Initialization
    //---------------------------------------------------------------------------------------------

    // Initialize the graph with a D3D12 device, command queue, and allocator pools.
    // Creates and owns the work CL and fixup CL. Pulls initial CAs from the pools.
    // The work CL is left in recording state; the fixup CL starts closed (opened at Dispatch).
    void Init(ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue, CCommandAllocatorPool* pAllocatorPool);

    //---------------------------------------------------------------------------------------------
    // Task Creation
    //---------------------------------------------------------------------------------------------

    // Create a new GPU task. The returned reference is valid until Reset().
    CGpuTask& CreateTask(const char* name = nullptr);

    // Declare texture usage for a task
    void DeclareTextureUsage(
        CGpuTask& task,
        CSurface12* pSurface,
        D3D12_BARRIER_LAYOUT requiredLayout,
        D3D12_BARRIER_SYNC sync,
        D3D12_BARRIER_ACCESS access,
        UINT subresources = 0xFFFFFFFF,
        D3D12_TEXTURE_BARRIER_FLAGS flags = D3D12_TEXTURE_BARRIER_FLAG_NONE);

    // Declare buffer usage for a task
    void DeclareBufferUsage(
        CGpuTask& task,
        CBuffer12* pBuffer,
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
    // use first-use semantics: the first task's required layout is assumed.
    void SetInitialLayout(CSurface12* pSurface, D3D12_BARRIER_LAYOUT layout, UINT subresource = 0xFFFFFFFF);

    // Insert a task into the graph: resolve barriers, emit into work CL, invoke RecordFunc.
    // Must be called in task creation order. The task's resource usages and dependencies
    // must be fully declared before calling this.
    void InsertTask(CGpuTask& task);

    // Resolve barriers for a task without emitting or invoking (for unit tests).
    // Returns barriers that would be emitted. Valid until next PrepareTask/InsertTask/Reset.
    const TaskBarriers& PrepareTask(CGpuTask& task);

    //---------------------------------------------------------------------------------------------
    // Dispatch
    //---------------------------------------------------------------------------------------------

    // Finalize the graph: close the work CL, compute fixup barriers into the owned
    // fixup CL, close the fixup CL, submit [fixup CL, work CL] via ExecuteCommandLists,
    // and update committed layouts on CSurface12 objects.
    // Call after all tasks have been inserted.
    void Dispatch();

    //---------------------------------------------------------------------------------------------
    // Queries
    //---------------------------------------------------------------------------------------------

    // Get the work command list (for callers that need direct CL access, e.g. DrawMesh).
    ID3D12GraphicsCommandList* GetWorkCommandList() const;

    // Get the fixup command list (for ECL submission after Dispatch).
    ID3D12GraphicsCommandList* GetFixupCommandList() const;

    // Get current work allocator (for reopening the work CL after it was closed).
    ID3D12CommandAllocator* GetWorkAllocator() const;

    // Get current fixup allocator.
    ID3D12CommandAllocator* GetFixupAllocator() const;

    // Get number of tasks
    uint32_t GetTaskCount() const;

    // Last error message from AddDependency or PrepareTask (empty if no error)
    const std::string& GetLastError() const;

    //---------------------------------------------------------------------------------------------
    // Lifecycle
    //---------------------------------------------------------------------------------------------

    // Compute final layouts from all tasks and update CSurface12::m_CurrentLayout.
    // Called internally by Dispatch().
    void ComputeFinalLayouts();

    // Reset the graph for next frame. Swaps allocators from pool.
    // fenceValue: the fence value covering this frame's GPU work.
    // completedFenceValue: the latest GPU-completed fence value.
    void Reset(UINT64 fenceValue, UINT64 completedFenceValue);

    // Release all pooled memory (scene transitions, shutdown). Next use rebuilds pools.
    void ReleaseMemory();

private:

    // Owned command lists (created by Init, released by ReleaseMemory)
    CComPtr<ID3D12GraphicsCommandList> m_pWorkCL;
    CComPtr<ID3D12GraphicsCommandList7> m_pWorkCL7;
    CComPtr<ID3D12GraphicsCommandList> m_pFixupCL;
    CComPtr<ID3D12GraphicsCommandList7> m_pFixupCL7;

    // Command queue for ECL submission (not owned — provided at Init)
    ID3D12CommandQueue* m_pCommandQueue = nullptr;

    // Allocator pool (not owned — provided at Init, used at Reset/Dispatch)
    CCommandAllocatorPool* m_pAllocatorPool = nullptr;

    // Current allocators (pulled from pools)
    CComPtr<ID3D12CommandAllocator> m_pWorkAllocator;
    CComPtr<ID3D12CommandAllocator> m_pFixupAllocator;

    // Task pool — grow-only deque (references remain valid across push_back).
    // Reused across frames via m_TaskCount.
    std::deque<CGpuTask> m_Tasks;
    uint32_t m_TaskCount = 0;

    // Surfaces used by this graph (for initial/final layout tracking)
    // Maps ID3D12Resource* → CSurface12* for committed layout access.
    std::unordered_map<ID3D12Resource*, CSurface12*> m_Surfaces;

    // Initial assumed layouts (recorded by first-use semantics for fixup barrier computation).
    // Per-resource SubresourceLayout tracks which layout the work CL assumed for each subresource.
    std::unordered_map<ID3D12Resource*, SubresourceLayout> m_ExpectedInitialLayouts;

    // Scratch buffer for PrepareTask results (avoids per-call allocation)
    TaskBarriers m_ScratchBarriers;

    // Last error from validation
    std::string m_LastError;

    // Whether Dispatch() was called this frame (Reset clears this)
    bool m_Dispatched = false;

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
inline GpuTextureUsage TextureUsageRenderTarget(CSurface12* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET, sub };
}
inline GpuTextureUsage TextureUsageDepthStencilWrite(CSurface12* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE, sub };
}
inline GpuTextureUsage TextureUsageDepthStencilRead(CSurface12* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ, D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ, sub };
}
inline GpuTextureUsage TextureUsagePixelShaderResource(CSurface12* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, sub };
}
inline GpuTextureUsage TextureUsageNonPixelShaderResource(CSurface12* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE, D3D12_BARRIER_SYNC_NON_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, sub };
}
inline GpuTextureUsage TextureUsageAllShaderResource(CSurface12* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE, D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, sub };
}
inline GpuTextureUsage TextureUsageCopySource(CSurface12* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE, sub };
}
inline GpuTextureUsage TextureUsageCopyDest(CSurface12* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST, sub };
}
inline GpuTextureUsage TextureUsageUnorderedAccess(CSurface12* p, UINT sub = 0xFFFFFFFF) {
    return { p, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, sub };
}
inline GpuTextureUsage TextureUsagePresent(CSurface12* p) {
    return { p, D3D12_BARRIER_LAYOUT_COMMON, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS, 0xFFFFFFFF };
}

// Buffer helpers
inline GpuBufferUsage BufferUsageVertexBuffer(CBuffer12* p) {
    return { p, D3D12_BARRIER_SYNC_VERTEX_SHADING, D3D12_BARRIER_ACCESS_VERTEX_BUFFER };
}
inline GpuBufferUsage BufferUsageIndexBuffer(CBuffer12* p) {
    return { p, D3D12_BARRIER_SYNC_INDEX_INPUT, D3D12_BARRIER_ACCESS_INDEX_BUFFER };
}
inline GpuBufferUsage BufferUsageConstantBuffer(CBuffer12* p) {
    return { p, D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_CONSTANT_BUFFER };
}
inline GpuBufferUsage BufferUsageCopySource(CBuffer12* p) {
    return { p, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE };
}
inline GpuBufferUsage BufferUsageCopyDest(CBuffer12* p) {
    return { p, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST };
}
inline GpuBufferUsage BufferUsageUnorderedAccess(CBuffer12* p) {
    return { p, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS };
}
inline GpuBufferUsage BufferUsageIndirectArgument(CBuffer12* p) {
    return { p, D3D12_BARRIER_SYNC_EXECUTE_INDIRECT, D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT };
}

} // namespace Canvas
