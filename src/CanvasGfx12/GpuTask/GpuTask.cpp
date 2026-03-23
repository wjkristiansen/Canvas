//================================================================================================
// GpuTask.cpp - GPU Task Graph Implementation
//================================================================================================

#include "pch.h"
#include "GpuTask.h"

namespace Canvas
{

//================================================================================================
// CGpuTaskGraph
//================================================================================================

CGpuTaskGraph::CGpuTaskGraph() = default;
CGpuTaskGraph::~CGpuTaskGraph() = default;

//------------------------------------------------------------------------------------------------
// Task Creation
//------------------------------------------------------------------------------------------------

GpuTaskHandle CGpuTaskGraph::CreateTask(const char* name)
{
    GpuTaskHandle handle = static_cast<GpuTaskHandle>(m_Tasks.size());
    CGpuTask& task = m_Tasks.emplace_back();
    task.Name = name;
    return handle;
}

void CGpuTaskGraph::DeclareTextureUsage(
    GpuTaskHandle task,
    ID3D12Resource* pResource,
    D3D12_BARRIER_LAYOUT requiredLayout,
    D3D12_BARRIER_SYNC sync,
    D3D12_BARRIER_ACCESS access,
    UINT subresources,
    D3D12_TEXTURE_BARRIER_FLAGS flags)
{
    assert(task < m_Tasks.size());
    assert(pResource != nullptr);

    GpuTextureUsage usage;
    usage.pResource = pResource;
    usage.RequiredLayout = requiredLayout;
    usage.Sync = sync;
    usage.Access = access;
    usage.Subresources = subresources;
    usage.Flags = flags;

    m_Tasks[task].TextureUsages.push_back(usage);
}

void CGpuTaskGraph::DeclareBufferUsage(
    GpuTaskHandle task,
    ID3D12Resource* pResource,
    D3D12_BARRIER_SYNC sync,
    D3D12_BARRIER_ACCESS access,
    UINT64 offset,
    UINT64 size)
{
    assert(task < m_Tasks.size());
    assert(pResource != nullptr);

    GpuBufferUsage usage;
    usage.pResource = pResource;
    usage.Sync = sync;
    usage.Access = access;
    usage.Offset = offset;
    usage.Size = size;

    m_Tasks[task].BufferUsages.push_back(usage);
}

void CGpuTaskGraph::AddExplicitDependency(GpuTaskHandle task, GpuTaskHandle dependency)
{
    assert(task < m_Tasks.size());
    assert(dependency < m_Tasks.size());
    assert(task != dependency);
    (void)task;
    (void)dependency;
    // Reserved for future use (e.g., ordering constraints not captured by resource usage).
}

//------------------------------------------------------------------------------------------------
// Initial Layout Configuration
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::SetInitialLayout(ID3D12Resource* pResource, D3D12_BARRIER_LAYOUT layout, UINT subresource)
{
    m_InitialLayouts[pResource].SetLayout(subresource, layout);
}

//------------------------------------------------------------------------------------------------
// Prepare Task
//
// Computes the task's sync/access scope (union of all declared usage), then resolves
// barriers against the accumulated per-resource state from all prior tasks. Returns the
// barriers to emit immediately before this task's commands.
//
// Barrier resolution logic:
//   - Per-resource state tracks PendingSync/PendingAccess since the last barrier.
//   - A barrier is needed when there's a layout change, or a write hazard (RAW/WAR/WAW).
//   - When a barrier is emitted, pending state resets to this task's usage.
//   - When no barrier is needed (compatible reads), pending state accumulates.
//
// Initial state (start of ECL): PendingSync=NONE, PendingAccess=NO_ACCESS.
// An ECL boundary guarantees all prior work is visible, so no sync is needed at the start.
// NOTE: NO_ACCESS (0x80000000) is a sentinel that must not be OR'd with other bits.
// The accumulation paths below handle this by assigning instead of OR'ing when
// PendingAccess is still NO_ACCESS.
//------------------------------------------------------------------------------------------------

const TaskBarriers& CGpuTaskGraph::PrepareTask(GpuTaskHandle task)
{
    assert(task < m_Tasks.size());

    EnsureResourceStateInitialized();

    auto& taskData = m_Tasks[task];

    // Reuse scratch buffer — clear but retain capacity
    m_ScratchBarriers.TaskHandle = task;
    m_ScratchBarriers.TextureBarriers.clear();
    m_ScratchBarriers.BufferBarriers.clear();

    // Compute accumulated sync/access scope for this task
    taskData.SyncScope = D3D12_BARRIER_SYNC_NONE;
    taskData.AccessScope = D3D12_BARRIER_ACCESS_NO_ACCESS;
    for (const auto& tex : taskData.TextureUsages)
    {
        taskData.SyncScope |= tex.Sync;
        taskData.AccessScope |= tex.Access;
    }
    for (const auto& buf : taskData.BufferUsages)
    {
        taskData.SyncScope |= buf.Sync;
        taskData.AccessScope |= buf.Access;
    }

    constexpr D3D12_BARRIER_ACCESS WRITE_ACCESS_MASK =
        D3D12_BARRIER_ACCESS_RENDER_TARGET |
        D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE |
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
        D3D12_BARRIER_ACCESS_COPY_DEST |
        D3D12_BARRIER_ACCESS_STREAM_OUTPUT;

    // Resolve texture barriers
    for (const auto& texUsage : taskData.TextureUsages)
    {
        auto stateIt = m_ResourceState.find(texUsage.pResource);
        if (stateIt == m_ResourceState.end())
        {
            // First access to this resource — record its first-use layout as the
            // assumed initial layout. No barrier emitted; the fixup CL at ECL time
            // will bridge committed state → this assumed layout.
            ResourceBarrierState& newState = m_ResourceState[texUsage.pResource];
            newState.Layouts.SetLayout(texUsage.Subresources, texUsage.RequiredLayout);
            newState.PendingSync = texUsage.Sync;
            newState.PendingAccess = texUsage.Access;

            m_InitialLayouts[texUsage.pResource].SetLayout(texUsage.Subresources, texUsage.RequiredLayout);
            continue;
        }

        auto& state = stateIt->second;

        bool pendingHasWrite = (state.PendingAccess & WRITE_ACCESS_MASK) != 0;
        bool newHasWrite = (texUsage.Access & WRITE_ACCESS_MASK) != 0;

        if (texUsage.Subresources == 0xFFFFFFFF)
        {
            D3D12_BARRIER_LAYOUT currentLayout = state.Layouts.GetLayout(0xFFFFFFFF);
            bool layoutChanged = (currentLayout != texUsage.RequiredLayout);
            bool needsBarrier = layoutChanged || pendingHasWrite || newHasWrite;

            if (needsBarrier)
            {
                if (!state.Layouts.UniformLayout.has_value() && !state.Layouts.PerSubresourceLayouts.empty() && layoutChanged)
                {
                    for (const auto& [sub, subLayout] : state.Layouts.PerSubresourceLayouts)
                    {
                        if (subLayout == texUsage.RequiredLayout && !pendingHasWrite && !newHasWrite)
                            continue;

                        ResolvedTextureBarrier b;
                        b.pResource = texUsage.pResource;
                        b.SyncBefore = state.PendingSync;
                        b.SyncAfter = texUsage.Sync;
                        b.AccessBefore = state.PendingAccess;
                        b.AccessAfter = texUsage.Access;
                        b.LayoutBefore = subLayout;
                        b.LayoutAfter = texUsage.RequiredLayout;
                        b.Subresources = sub;
                        b.Flags = texUsage.Flags;
                        m_ScratchBarriers.TextureBarriers.push_back(b);
                    }
                }
                else
                {
                    ResolvedTextureBarrier b;
                    b.pResource = texUsage.pResource;
                    b.SyncBefore = state.PendingSync;
                    b.SyncAfter = texUsage.Sync;
                    b.AccessBefore = state.PendingAccess;
                    b.AccessAfter = texUsage.Access;
                    b.LayoutBefore = currentLayout;
                    b.LayoutAfter = texUsage.RequiredLayout;
                    b.Subresources = 0xFFFFFFFF;
                    b.Flags = texUsage.Flags;
                    m_ScratchBarriers.TextureBarriers.push_back(b);
                }

                state.Layouts.SetLayout(0xFFFFFFFF, texUsage.RequiredLayout);
                state.PendingSync = texUsage.Sync;
                state.PendingAccess = texUsage.Access;
            }
            else
            {
                if (state.PendingAccess == D3D12_BARRIER_ACCESS_NO_ACCESS)
                {
                    state.PendingSync = texUsage.Sync;
                    state.PendingAccess = texUsage.Access;
                }
                else
                {
                    state.PendingSync |= texUsage.Sync;
                    state.PendingAccess |= texUsage.Access;
                }
            }
        }
        else
        {
            D3D12_BARRIER_LAYOUT currentLayout = state.Layouts.GetLayout(texUsage.Subresources);
            bool layoutChanged = (currentLayout != texUsage.RequiredLayout);
            bool needsBarrier = layoutChanged || pendingHasWrite || newHasWrite;

            if (needsBarrier)
            {
                ResolvedTextureBarrier b;
                b.pResource = texUsage.pResource;
                b.SyncBefore = state.PendingSync;
                b.SyncAfter = texUsage.Sync;
                b.AccessBefore = state.PendingAccess;
                b.AccessAfter = texUsage.Access;
                b.LayoutBefore = currentLayout;
                b.LayoutAfter = texUsage.RequiredLayout;
                b.Subresources = texUsage.Subresources;
                b.Flags = texUsage.Flags;
                m_ScratchBarriers.TextureBarriers.push_back(b);

                state.Layouts.SetLayout(texUsage.Subresources, texUsage.RequiredLayout);
                state.PendingSync = texUsage.Sync;
                state.PendingAccess = texUsage.Access;
            }
            else
            {
                if (state.PendingAccess == D3D12_BARRIER_ACCESS_NO_ACCESS)
                {
                    state.PendingSync = texUsage.Sync;
                    state.PendingAccess = texUsage.Access;
                }
                else
                {
                    state.PendingSync |= texUsage.Sync;
                    state.PendingAccess |= texUsage.Access;
                }
            }
        }
    }

    // Resolve buffer barriers
    for (const auto& bufUsage : taskData.BufferUsages)
    {
        auto stateIt = m_ResourceState.find(bufUsage.pResource);
        if (stateIt == m_ResourceState.end())
        {
            ResourceBarrierState& newState = m_ResourceState[bufUsage.pResource];
            newState.PendingSync = bufUsage.Sync;
            newState.PendingAccess = bufUsage.Access;
            continue;
        }

        auto& state = stateIt->second;

        bool pendingHasWrite = (state.PendingAccess & WRITE_ACCESS_MASK) != 0;
        bool newHasWrite = (bufUsage.Access & WRITE_ACCESS_MASK) != 0;
        bool needsBarrier = pendingHasWrite || newHasWrite;

        if (needsBarrier)
        {
            ResolvedBufferBarrier b;
            b.pResource = bufUsage.pResource;
            b.SyncBefore = state.PendingSync;
            b.SyncAfter = bufUsage.Sync;
            b.AccessBefore = state.PendingAccess;
            b.AccessAfter = bufUsage.Access;
            b.Offset = bufUsage.Offset;
            b.Size = bufUsage.Size;
            m_ScratchBarriers.BufferBarriers.push_back(b);

            state.PendingSync = bufUsage.Sync;
            state.PendingAccess = bufUsage.Access;
        }
        else
        {
            if (state.PendingAccess == D3D12_BARRIER_ACCESS_NO_ACCESS)
            {
                state.PendingSync = bufUsage.Sync;
                state.PendingAccess = bufUsage.Access;
            }
            else
            {
                state.PendingSync |= bufUsage.Sync;
                state.PendingAccess |= bufUsage.Access;
            }
        }
    }

    return m_ScratchBarriers;
}

//------------------------------------------------------------------------------------------------
void CGpuTaskGraph::EnsureResourceStateInitialized()
{
    if (!m_ResourceStateInitialized)
    {
        // Initialize per-resource barrier state from initial layouts
        // PendingSync/PendingAccess start at NONE/NO_ACCESS (ECL boundary = implicit full sync)
        for (const auto& [pResource, layoutState] : m_InitialLayouts)
        {
            m_ResourceState[pResource].Layouts = layoutState;
        }
        m_ResourceStateInitialized = true;
    }
}

//------------------------------------------------------------------------------------------------
// Compute Final Layouts
//
// The final layout of each resource is the layout it was transitioned to by its last
// usage in creation order (which is execution order in forward-order mode).
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::ComputeFinalLayouts()
{
    // Build final layouts from resource state (already tracked by PrepareTask).
    // No map copy needed — m_ResourceState has the latest layout for every touched resource.
    m_FinalLayouts.clear();
    for (const auto& [pResource, state] : m_ResourceState)
    {
        m_FinalLayouts[pResource] = state.Layouts;
    }
}

//------------------------------------------------------------------------------------------------
// Queries
//------------------------------------------------------------------------------------------------

const std::unordered_map<ID3D12Resource*, GpuTaskGraphLayoutState>& CGpuTaskGraph::GetFinalLayouts() const
{
    return m_FinalLayouts;
}

const std::unordered_map<ID3D12Resource*, GpuTaskGraphLayoutState>& CGpuTaskGraph::GetInitialLayouts() const
{
    return m_InitialLayouts;
}

const CGpuTask& CGpuTaskGraph::GetTask(GpuTaskHandle task) const
{
    assert(task < m_Tasks.size());
    return m_Tasks[task];
}

uint32_t CGpuTaskGraph::GetTaskCount() const
{
    return static_cast<uint32_t>(m_Tasks.size());
}

std::optional<D3D12_BARRIER_LAYOUT> CGpuTaskGraph::GetCurrentLayout(ID3D12Resource* pResource, UINT subresource) const
{
    auto it = m_ResourceState.find(pResource);
    if (it == m_ResourceState.end())
    {
        // Not touched by any task — check initial layouts
        auto initIt = m_InitialLayouts.find(pResource);
        if (initIt != m_InitialLayouts.end())
            return initIt->second.GetLayout(subresource);
        return std::nullopt;
    }
    return it->second.Layouts.GetLayout(subresource);
}

//------------------------------------------------------------------------------------------------
// Lifecycle
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::Reset()
{
    m_Tasks.clear();
    m_InitialLayouts.clear();
    m_FinalLayouts.clear();
    m_ResourceState.clear();
    m_ResourceStateInitialized = false;
}

} // namespace Canvas
