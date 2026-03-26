//================================================================================================
// GpuTask.cpp - GPU Task Graph with Explicit Dependencies
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

CGpuTask& CGpuTaskGraph::CreateTask(const char* name)
{
    if (m_TaskCount < m_Tasks.size())
    {
        CGpuTask& task = m_Tasks[m_TaskCount++];
        task.Name = name ? name : "";
        task.Index = m_TaskCount - 1;
        task.Dependencies.clear();
        task.TextureUsages.clear();
        task.BufferUsages.clear();
        task.ResourceStates.clear();
        return task;
    }

    CGpuTask& task = m_Tasks.emplace_back();
    m_TaskCount++;
    task.Name = name ? name : "";
    task.Index = m_TaskCount - 1;
    return task;
}

void CGpuTaskGraph::DeclareTextureUsage(
    CGpuTask& task,
    ID3D12Resource* pResource,
    D3D12_BARRIER_LAYOUT requiredLayout,
    D3D12_BARRIER_SYNC sync,
    D3D12_BARRIER_ACCESS access,
    UINT subresources,
    D3D12_TEXTURE_BARRIER_FLAGS flags)
{
    assert(pResource != nullptr);

    GpuTextureUsage usage;
    usage.pResource = pResource;
    usage.RequiredLayout = requiredLayout;
    usage.Sync = sync;
    usage.Access = access;
    usage.Subresources = subresources;
    usage.Flags = flags;

    task.TextureUsages.push_back(usage);
}

void CGpuTaskGraph::DeclareBufferUsage(
    CGpuTask& task,
    ID3D12Resource* pResource,
    D3D12_BARRIER_SYNC sync,
    D3D12_BARRIER_ACCESS access,
    UINT64 offset,
    UINT64 size)
{
    assert(pResource != nullptr);

    GpuBufferUsage usage;
    usage.pResource = pResource;
    usage.Sync = sync;
    usage.Access = access;
    usage.Offset = offset;
    usage.Size = size;

    task.BufferUsages.push_back(usage);
}

void CGpuTaskGraph::AddDependency(CGpuTask& task, const CGpuTask& dependency)
{
    if (&task == &dependency)
    {
        m_LastError = "AddDependency: task '" + task.Name + "' cannot depend on itself";
        return;
    }
    if (dependency.Index >= task.Index)
    {
        m_LastError = "AddDependency: task '" + task.Name + "' cannot depend on '" + dependency.Name + "' (dependency must be created before dependent task)";
        return;
    }
    task.Dependencies.push_back(&dependency);
}

//------------------------------------------------------------------------------------------------
// Initial Layout Configuration
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::SetInitialLayout(ID3D12Resource* pResource, D3D12_BARRIER_LAYOUT layout, UINT subresource)
{
    m_InitialLayouts[pResource].SetLayout(subresource, layout);
}

//------------------------------------------------------------------------------------------------
// Prepare Task — Dependency-Driven Barrier Resolution with Inherited State Table
//
// 1. Build the task's resource state table by inheriting from dependencies
// 2. For each resource usage, look up prior state in the table
// 3. Emit barriers for hazards (layout change, RAW, WAR, WAW)
// 4. Update the table with this task's usages for downstream tasks
//------------------------------------------------------------------------------------------------

const TaskBarriers& CGpuTaskGraph::PrepareTask(CGpuTask& taskData)
{
    ValidateDependencyConflicts(taskData);

    m_ScratchBarriers.TextureBarriers.clear();
    m_ScratchBarriers.BufferBarriers.clear();

    // Step 1: Build inherited resource state table from dependencies
    taskData.ResourceStates.clear();
    if (taskData.Dependencies.size() == 1)
    {
        // Single dep: copy its table
        taskData.ResourceStates = taskData.Dependencies[0]->ResourceStates;
    }
    else if (taskData.Dependencies.size() > 1)
    {
        // Multiple deps (join): merge tables. Explicit entries (from dep's own
        // usages) are preferred over inherited entries at merge time.
        for (const CGpuTask* pDep : taskData.Dependencies)
        {
            for (const auto& entry : pDep->ResourceStates)
            {
                UnionResourceState(taskData.ResourceStates, entry);
            }
        }
    }
    // No deps: empty table — all resources fall through to initial/committed state

    // All entries from deps are inherited. This task's own usages
    // will be marked Inherited=false below.
    for (auto& entry : taskData.ResourceStates)
        entry.Inherited = true;

    constexpr D3D12_BARRIER_ACCESS WRITE_ACCESS_MASK =
        D3D12_BARRIER_ACCESS_RENDER_TARGET |
        D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE |
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
        D3D12_BARRIER_ACCESS_COPY_DEST |
        D3D12_BARRIER_ACCESS_STREAM_OUTPUT;

    // Step 2-3: Resolve texture barriers
    for (const auto& texUsage : taskData.TextureUsages)
    {
        const ResourceStateEntry* pPrior = FindResourceState(taskData.ResourceStates, texUsage.pResource, texUsage.Subresources);

        D3D12_BARRIER_SYNC syncBefore = D3D12_BARRIER_SYNC_NONE;
        D3D12_BARRIER_ACCESS accessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
        D3D12_BARRIER_LAYOUT layoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
        bool priorHasWrite = false;
        bool hasPrior = (pPrior != nullptr);

        if (hasPrior)
        {
            syncBefore = pPrior->Sync;
            accessBefore = pPrior->Access;
            layoutBefore = pPrior->Layout;
            priorHasWrite = (accessBefore & WRITE_ACCESS_MASK) != 0;
        }
        else
        {
            auto initIt = m_InitialLayouts.find(texUsage.pResource);
            if (initIt != m_InitialLayouts.end())
            {
                layoutBefore = initIt->second.GetLayout(texUsage.Subresources);
            }
            else
            {
                // First-use semantics: assume the required layout, record for fixup CL
                m_InitialLayouts[texUsage.pResource].SetLayout(
                    texUsage.Subresources, texUsage.RequiredLayout);
                layoutBefore = texUsage.RequiredLayout;
            }
        }

        bool newHasWrite = (texUsage.Access & WRITE_ACCESS_MASK) != 0;
        bool layoutChanged = (layoutBefore != texUsage.RequiredLayout);
        bool needsBarrier = layoutChanged || priorHasWrite || (hasPrior && newHasWrite);

        if (needsBarrier)
        {
            ResolvedTextureBarrier b;
            b.pResource = texUsage.pResource;
            b.SyncBefore = syncBefore;
            b.SyncAfter = texUsage.Sync;
            b.AccessBefore = accessBefore;
            b.AccessAfter = texUsage.Access;
            b.LayoutBefore = layoutBefore;
            b.LayoutAfter = texUsage.RequiredLayout;
            b.Subresources = texUsage.Subresources;
            b.Flags = texUsage.Flags;
            m_ScratchBarriers.TextureBarriers.push_back(b);
        }

        // Step 4: Update state table with this task's usage
        MergeResourceState(taskData.ResourceStates,
            { texUsage.pResource, texUsage.Subresources, texUsage.Sync, texUsage.Access, texUsage.RequiredLayout, false });
    }

    // Resolve buffer barriers
    for (const auto& bufUsage : taskData.BufferUsages)
    {
        const ResourceStateEntry* pPrior = FindResourceState(taskData.ResourceStates, bufUsage.pResource, 0xFFFFFFFF);

        D3D12_BARRIER_SYNC syncBefore = D3D12_BARRIER_SYNC_NONE;
        D3D12_BARRIER_ACCESS accessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
        bool priorHasWrite = false;
        bool hasPrior = (pPrior != nullptr);

        if (hasPrior)
        {
            syncBefore = pPrior->Sync;
            accessBefore = pPrior->Access;
            priorHasWrite = (accessBefore & WRITE_ACCESS_MASK) != 0;
        }

        bool newHasWrite = (bufUsage.Access & WRITE_ACCESS_MASK) != 0;
        bool needsBarrier = priorHasWrite || (hasPrior && newHasWrite);

        if (needsBarrier)
        {
            ResolvedBufferBarrier b;
            b.pResource = bufUsage.pResource;
            b.SyncBefore = syncBefore;
            b.SyncAfter = bufUsage.Sync;
            b.AccessBefore = accessBefore;
            b.AccessAfter = bufUsage.Access;
            b.Offset = bufUsage.Offset;
            b.Size = bufUsage.Size;
            m_ScratchBarriers.BufferBarriers.push_back(b);
        }

        MergeResourceState(taskData.ResourceStates,
            { bufUsage.pResource, 0xFFFFFFFF, bufUsage.Sync, bufUsage.Access, D3D12_BARRIER_LAYOUT_COMMON, false });
    }

    return m_ScratchBarriers;
}

//------------------------------------------------------------------------------------------------
// Resource State Table Helpers
//------------------------------------------------------------------------------------------------

const ResourceStateEntry* CGpuTaskGraph::FindResourceState(
    const std::vector<ResourceStateEntry>& table, ID3D12Resource* pResource, UINT subresource)
{
    const ResourceStateEntry* pUniform = nullptr;
    for (const auto& entry : table)
    {
        if (entry.pResource == pResource)
        {
            if (entry.Subresource == subresource)
                return &entry;  // exact match
            if (entry.Subresource == 0xFFFFFFFF)
                pUniform = &entry;  // uniform fallback
        }
    }
    return pUniform;  // nullptr if no match at all
}

void CGpuTaskGraph::MergeResourceState(
    std::vector<ResourceStateEntry>& table, const ResourceStateEntry& entry)
{
    for (auto& existing : table)
    {
        if (existing.pResource == entry.pResource && existing.Subresource == entry.Subresource)
        {
            existing.Sync = entry.Sync;
            existing.Access = entry.Access;
            existing.Layout = entry.Layout;
            existing.Inherited = entry.Inherited;
            return;
        }
    }
    table.push_back(entry);
}

void CGpuTaskGraph::UnionResourceState(
    std::vector<ResourceStateEntry>& table, const ResourceStateEntry& entry)
{
    for (auto& existing : table)
    {
        if (existing.pResource == entry.pResource && existing.Subresource == entry.Subresource)
        {
            // Prefer non-inherited (task's own usage) over inherited (from dep chain)
            if (!entry.Inherited && existing.Inherited)
            {
                existing = entry;
                return;
            }
            if (entry.Inherited && !existing.Inherited)
            {
                return;  // keep existing non-inherited entry
            }
            // Both same inheritance level: union sync/access, take latest layout
            existing.Sync |= entry.Sync;
            if (existing.Access == D3D12_BARRIER_ACCESS_NO_ACCESS)
                existing.Access = entry.Access;
            else if (entry.Access != D3D12_BARRIER_ACCESS_NO_ACCESS)
                existing.Access |= entry.Access;
            existing.Layout = entry.Layout;
            return;
        }
    }
    table.push_back(entry);
}

//------------------------------------------------------------------------------------------------
// Validate Dependency Conflicts
//
// Detects ambiguous resource states at DAG join points. When a task depends on multiple
// tasks that both touch the same resource, we check:
//   1. Multiple writers — if two unordered deps both write a resource, the final state
//      is ambiguous. The caller should order them so only the last writer is a direct dep.
//   2. Conflicting texture layouts — if two deps leave a texture in different layouts,
//      there's no valid LayoutBefore for the joining task's barrier.
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::ValidateDependencyConflicts(const CGpuTask& task)
{
    if (task.Dependencies.size() < 2)
        return;

    constexpr D3D12_BARRIER_ACCESS WRITE_ACCESS_MASK =
        D3D12_BARRIER_ACCESS_RENDER_TARGET |
        D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE |
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
        D3D12_BARRIER_ACCESS_COPY_DEST |
        D3D12_BARRIER_ACCESS_STREAM_OUTPUT;

    struct DepResourceInfo
    {
        const CGpuTask* pWriterTask = nullptr;
        D3D12_BARRIER_LAYOUT TextureLayout = D3D12_BARRIER_LAYOUT_COMMON;
        bool HasTextureLayout = false;
    };
    std::unordered_map<ID3D12Resource*, DepResourceInfo> resourceMap;

    for (const CGpuTask* pDep : task.Dependencies)
    {
        assert(pDep != nullptr);

        for (const auto& tex : pDep->TextureUsages)
        {
            auto& info = resourceMap[tex.pResource];
            bool isWrite = (tex.Access & WRITE_ACCESS_MASK) != 0;

            if (isWrite && info.pWriterTask != nullptr)
            {
                m_LastError = "DAG conflict in task '" + task.Name +
                    "': dependencies '" + info.pWriterTask->Name + "' and '" +
                    pDep->Name + "' both write to the same texture. "
                    "Order them so only the final writer is a direct dependency.";
                return;
            }
            if (isWrite)
                info.pWriterTask = pDep;

            if (info.HasTextureLayout && info.TextureLayout != tex.RequiredLayout)
            {
                m_LastError = "DAG conflict in task '" + task.Name +
                    "': dependencies leave a texture in conflicting layouts. "
                    "Cannot determine a valid LayoutBefore for the barrier.";
                return;
            }
            info.TextureLayout = tex.RequiredLayout;
            info.HasTextureLayout = true;
        }

        for (const auto& buf : pDep->BufferUsages)
        {
            auto& info = resourceMap[buf.pResource];
            bool isWrite = (buf.Access & WRITE_ACCESS_MASK) != 0;

            if (isWrite && info.pWriterTask != nullptr)
            {
                m_LastError = "DAG conflict in task '" + task.Name +
                    "': dependencies '" + info.pWriterTask->Name + "' and '" +
                    pDep->Name + "' both write to the same buffer. "
                    "Order them so only the final writer is a direct dependency.";
                return;
            }
            if (isWrite)
                info.pWriterTask = pDep;
        }
    }
}

//------------------------------------------------------------------------------------------------
// Compute Final Layouts
//
// Walks all tasks in creation order. The last task to touch each resource
// determines its final layout (from its declared texture usages).
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::ComputeFinalLayouts()
{
    m_FinalLayouts.clear();
    for (uint32_t i = 0; i < m_TaskCount; ++i)
    {
        const CGpuTask& task = m_Tasks[i];
        for (const auto& tex : task.TextureUsages)
        {
            m_FinalLayouts[tex.pResource].SetLayout(tex.Subresources, tex.RequiredLayout);
        }
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

uint32_t CGpuTaskGraph::GetTaskCount() const
{
    return m_TaskCount;
}

const std::string& CGpuTaskGraph::GetLastError() const
{
    return m_LastError;
}

//------------------------------------------------------------------------------------------------
// Lifecycle
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::Reset()
{
    m_TaskCount = 0;
    m_InitialLayouts.clear();
    m_FinalLayouts.clear();
    m_LastError.clear();
}

void CGpuTaskGraph::ReleaseMemory()
{
    Reset();
    m_Tasks.clear();
    m_Tasks.shrink_to_fit();
    m_ScratchBarriers.TextureBarriers.clear();
    m_ScratchBarriers.TextureBarriers.shrink_to_fit();
    m_ScratchBarriers.BufferBarriers.clear();
    m_ScratchBarriers.BufferBarriers.shrink_to_fit();
}

} // namespace Canvas
