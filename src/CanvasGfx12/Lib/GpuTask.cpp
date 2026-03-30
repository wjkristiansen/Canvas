//================================================================================================
// GpuTask.cpp - GPU Task Graph with Explicit Dependencies
//================================================================================================

#include "pch.h"
#include "GpuTask.h"
#include "Surface12.h"
#include "Buffer12.h"

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
        task.RecordFunc = nullptr;
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
    CSurface12* pSurface,
    D3D12_BARRIER_LAYOUT requiredLayout,
    D3D12_BARRIER_SYNC sync,
    D3D12_BARRIER_ACCESS access,
    UINT subresources,
    D3D12_TEXTURE_BARRIER_FLAGS flags)
{
    assert(pSurface != nullptr);

    GpuTextureUsage usage;
    usage.pSurface = pSurface;
    usage.RequiredLayout = requiredLayout;
    usage.Sync = sync;
    usage.Access = access;
    usage.Subresources = subresources;
    usage.Flags = flags;

    task.TextureUsages.push_back(usage);

    // Register surface for committed layout access during Dispatch
    m_Surfaces[pSurface->GetD3DResource()] = pSurface;
}

void CGpuTaskGraph::DeclareBufferUsage(
    CGpuTask& task,
    CBuffer12* pBuffer,
    D3D12_BARRIER_SYNC sync,
    D3D12_BARRIER_ACCESS access,
    UINT64 offset,
    UINT64 size)
{
    assert(pBuffer != nullptr);

    GpuBufferUsage usage;
    usage.pBuffer = pBuffer;
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
// Initialization
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::Init(ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue, CCommandAllocatorPool* pAllocatorPool)
{
    m_pCommandQueue = pCommandQueue;
    m_pAllocatorPool = pAllocatorPool;

    pAllocatorPool->SwapAllocator(m_pWorkAllocator, 0, 0);
    pAllocatorPool->SwapAllocator(m_pFixupAllocator, 0, 0);

    pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pWorkAllocator, nullptr, IID_PPV_ARGS(&m_pWorkCL));
    m_pWorkCL->Close();
    m_pWorkCL->QueryInterface(IID_PPV_ARGS(&m_pWorkCL7));

    pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pFixupAllocator, nullptr, IID_PPV_ARGS(&m_pFixupCL));
    m_pFixupCL->Close();
    m_pFixupCL->QueryInterface(IID_PPV_ARGS(&m_pFixupCL7));
}

//------------------------------------------------------------------------------------------------
// Queries
//------------------------------------------------------------------------------------------------

ID3D12GraphicsCommandList* CGpuTaskGraph::GetWorkCommandList() const
{
    return m_pWorkCL;
}

ID3D12GraphicsCommandList* CGpuTaskGraph::GetFixupCommandList() const
{
    return m_pFixupCL;
}

ID3D12CommandAllocator* CGpuTaskGraph::GetWorkAllocator() const
{
    return m_pWorkAllocator;
}

ID3D12CommandAllocator* CGpuTaskGraph::GetFixupAllocator() const
{
    return m_pFixupAllocator;
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
// D3D12 Barrier Emission Helper
//------------------------------------------------------------------------------------------------

static void EmitBarriersToCommandList(ID3D12GraphicsCommandList7* pCL, const TaskBarriers& barriers)
{
    if (!pCL)
        return;

    if (!barriers.TextureBarriers.empty())
    {
        std::vector<D3D12_TEXTURE_BARRIER> d3dBarriers;
        d3dBarriers.reserve(barriers.TextureBarriers.size());
        for (const auto& tb : barriers.TextureBarriers)
        {
            D3D12_TEXTURE_BARRIER d3d = {};
            d3d.SyncBefore = tb.SyncBefore;
            d3d.SyncAfter = tb.SyncAfter;
            d3d.AccessBefore = tb.AccessBefore;
            d3d.AccessAfter = tb.AccessAfter;
            d3d.LayoutBefore = tb.LayoutBefore;
            d3d.LayoutAfter = tb.LayoutAfter;
            d3d.pResource = tb.pSurface->GetD3DResource();
            d3d.Subresources.IndexOrFirstMipLevel = tb.Subresources;
            d3d.Flags = tb.Flags;
            d3dBarriers.push_back(d3d);
        }
        D3D12_BARRIER_GROUP group = {};
        group.Type = D3D12_BARRIER_TYPE_TEXTURE;
        group.NumBarriers = static_cast<UINT>(d3dBarriers.size());
        group.pTextureBarriers = d3dBarriers.data();
        pCL->Barrier(1, &group);
    }

    if (!barriers.BufferBarriers.empty())
    {
        std::vector<D3D12_BUFFER_BARRIER> d3dBarriers;
        d3dBarriers.reserve(barriers.BufferBarriers.size());
        for (const auto& bb : barriers.BufferBarriers)
        {
            D3D12_BUFFER_BARRIER d3d = {};
            d3d.SyncBefore = bb.SyncBefore;
            d3d.SyncAfter = bb.SyncAfter;
            d3d.AccessBefore = bb.AccessBefore;
            d3d.AccessAfter = bb.AccessAfter;
            d3d.pResource = bb.pBuffer->GetD3DResource();
            d3d.Offset = bb.Offset;
            d3d.Size = bb.Size;
            d3dBarriers.push_back(d3d);
        }
        D3D12_BARRIER_GROUP group = {};
        group.Type = D3D12_BARRIER_TYPE_BUFFER;
        group.NumBarriers = static_cast<UINT>(d3dBarriers.size());
        group.pBufferBarriers = d3dBarriers.data();
        pCL->Barrier(1, &group);
    }
}

//------------------------------------------------------------------------------------------------
// Insert Task - Atomic barrier resolution + emission + recording
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::InsertTask(CGpuTask& task)
{
    const TaskBarriers& barriers = PrepareTask(task);
    Canvas::EmitBarriersToCommandList(m_pWorkCL7, barriers);
    if (task.RecordFunc && m_pWorkCL)
        task.RecordFunc(m_pWorkCL);
}

//------------------------------------------------------------------------------------------------
// Prepare Task - Dependency-Driven Barrier Resolution with Inherited State Table
//------------------------------------------------------------------------------------------------

const TaskBarriers& CGpuTaskGraph::PrepareTask(CGpuTask& taskData)
{
#ifndef NDEBUG
    ValidateDependencyConflicts(taskData);
#endif

    m_ScratchBarriers.TextureBarriers.clear();
    m_ScratchBarriers.BufferBarriers.clear();

    // Step 1: Build inherited resource state table.
    // Baseline from previous task (implicit CL ordering), then merge explicit deps.
    taskData.ResourceStates.clear();
    if (taskData.Index > 0)
    {
        const CGpuTask& prevTask = m_Tasks[taskData.Index - 1];
        taskData.ResourceStates = prevTask.ResourceStates;
    }
    for (const CGpuTask* pDep : taskData.Dependencies)
    {
        for (const auto& entry : pDep->ResourceStates)
            UnionResourceState(taskData.ResourceStates, entry);
    }
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
        ID3D12Resource* pD3DResource = texUsage.pSurface->GetD3DResource();
        const ResourceStateEntry* pPrior = FindResourceState(taskData.ResourceStates, pD3DResource, texUsage.Subresources);

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
            // No prior state in this ECL scope.
            // Record assumed initial layout for fixup barrier computation.
            auto& assumed = m_ExpectedInitialLayouts[pD3DResource];
            if (assumed.m_UniformLayout == D3D12_BARRIER_LAYOUT_UNDEFINED && assumed.m_AllSame)
            {
                // First time seeing this resource — initialize from required layout
                assumed = SubresourceLayout(texUsage.RequiredLayout, texUsage.pSurface->m_NumSubresources);
            }
            else if (texUsage.Subresources != 0xFFFFFFFF)
            {
                // Per-subresource first-use on a resource we've seen before
                assumed.SetLayout(texUsage.Subresources, texUsage.RequiredLayout, texUsage.pSurface->m_NumSubresources);
            }

            layoutBefore = texUsage.RequiredLayout;  // first-use: assume required layout
        }

        bool newHasWrite = (texUsage.Access & WRITE_ACCESS_MASK) != 0;
        bool layoutChanged = (layoutBefore != texUsage.RequiredLayout);
        bool needsBarrier = layoutChanged || priorHasWrite || (hasPrior && newHasWrite);

        if (needsBarrier)
        {
            ResolvedTextureBarrier b;
            b.pSurface = texUsage.pSurface;
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

        MergeResourceState(taskData.ResourceStates,
            { pD3DResource, texUsage.Subresources, texUsage.Sync, texUsage.Access, texUsage.RequiredLayout, false });
    }

    // Resolve buffer barriers
    for (const auto& bufUsage : taskData.BufferUsages)
    {
        ID3D12Resource* pD3DResource = bufUsage.pBuffer->GetD3DResource();
        const ResourceStateEntry* pPrior = FindResourceState(taskData.ResourceStates, pD3DResource, 0xFFFFFFFF);

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
            b.pBuffer = bufUsage.pBuffer;
            b.SyncBefore = syncBefore;
            b.SyncAfter = bufUsage.Sync;
            b.AccessBefore = accessBefore;
            b.AccessAfter = bufUsage.Access;
            b.Offset = bufUsage.Offset;
            b.Size = bufUsage.Size;
            m_ScratchBarriers.BufferBarriers.push_back(b);
        }

        MergeResourceState(taskData.ResourceStates,
            { pD3DResource, 0xFFFFFFFF, bufUsage.Sync, bufUsage.Access, D3D12_BARRIER_LAYOUT_COMMON, false });
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
                return &entry;
            if (entry.Subresource == 0xFFFFFFFF)
                pUniform = &entry;
        }
    }
    return pUniform;
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
            if (!entry.Inherited && existing.Inherited)
            {
                existing = entry;
                return;
            }
            if (entry.Inherited && !existing.Inherited)
                return;
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
// Validate Dependency Conflicts (debug only)
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
        for (const auto& tex : pDep->TextureUsages)
        {
            auto* pD3DResource = tex.pSurface->GetD3DResource();
            auto& info = resourceMap[pD3DResource];
            bool isWrite = (tex.Access & WRITE_ACCESS_MASK) != 0;

            if (isWrite && info.pWriterTask != nullptr)
            {
                m_LastError = "DAG conflict in task '" + task.Name +
                    "': dependencies '" + info.pWriterTask->Name + "' and '" +
                    pDep->Name + "' both write to the same texture.";
                return;
            }
            if (isWrite) info.pWriterTask = pDep;
            if (info.HasTextureLayout && info.TextureLayout != tex.RequiredLayout)
            {
                m_LastError = "DAG conflict in task '" + task.Name +
                    "': dependencies leave a texture in conflicting layouts.";
                return;
            }
            info.TextureLayout = tex.RequiredLayout;
            info.HasTextureLayout = true;
        }

        for (const auto& buf : pDep->BufferUsages)
        {
            auto* pD3DResource = buf.pBuffer->GetD3DResource();
            auto& info = resourceMap[pD3DResource];
            bool isWrite = (buf.Access & WRITE_ACCESS_MASK) != 0;
            if (isWrite && info.pWriterTask != nullptr)
            {
                m_LastError = "DAG conflict in task '" + task.Name +
                    "': dependencies both write to the same buffer.";
                return;
            }
            if (isWrite) info.pWriterTask = pDep;
        }
    }
}

//------------------------------------------------------------------------------------------------
// Dispatch - Finalize graph, compute fixup barriers, close CLs
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::Dispatch()
{
    if (m_pWorkCL)
        m_pWorkCL->Close();

    // Compute fixup barriers FIRST — before ComputeFinalLayouts overwrites committed state.
    // The fixup CL bridges the gap between actual committed layout (on CSurface12) and the
    // assumed initial layout that the work CL was recorded against.
    std::vector<D3D12_TEXTURE_BARRIER> d3dBarriers;

    for (const auto& [pD3DResource, assumedLayouts] : m_ExpectedInitialLayouts)
    {
        auto surfIt = m_Surfaces.find(pD3DResource);
        if (surfIt == m_Surfaces.end()) continue;

        CSurface12* pSurface = surfIt->second;
        const auto& committed = pSurface->m_CurrentLayout;

        // Find first task's sync/access for this resource (for fixup barrier sync fields)
        D3D12_BARRIER_SYNC syncAfter = D3D12_BARRIER_SYNC_ALL;
        D3D12_BARRIER_ACCESS accessAfter = D3D12_BARRIER_ACCESS_COMMON;
        for (uint32_t i = 0; i < m_TaskCount; ++i)
        {
            for (const auto& tex : m_Tasks[i].TextureUsages)
            {
                if (tex.pSurface->GetD3DResource() == pD3DResource)
                {
                    syncAfter = tex.Sync;
                    accessAfter = tex.Access;
                    goto found_sync;
                }
            }
        }
        found_sync:

        if (committed.m_AllSame && assumedLayouts.m_AllSame)
        {
            // Both uniform — at most one barrier for all subresources
            if (committed.m_UniformLayout != assumedLayouts.m_UniformLayout)
            {
                D3D12_TEXTURE_BARRIER tb = {};
                tb.SyncBefore = D3D12_BARRIER_SYNC_NONE;
                tb.SyncAfter = syncAfter;
                tb.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
                tb.AccessAfter = accessAfter;
                tb.LayoutBefore = committed.m_UniformLayout;
                tb.LayoutAfter = assumedLayouts.m_UniformLayout;
                tb.pResource = pD3DResource;
                tb.Subresources.IndexOrFirstMipLevel = 0xFFFFFFFF;
                tb.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
                d3dBarriers.push_back(tb);
            }
        }
        else
        {
            // At least one side is per-subresource — compare each subresource
            UINT numSub = pSurface->m_NumSubresources;
            for (UINT sub = 0; sub < numSub; ++sub)
            {
                D3D12_BARRIER_LAYOUT committedSub = committed.GetLayout(sub);
                D3D12_BARRIER_LAYOUT assumedSub = assumedLayouts.GetLayout(sub);

                if (committedSub != assumedSub)
                {
                    D3D12_TEXTURE_BARRIER tb = {};
                    tb.SyncBefore = D3D12_BARRIER_SYNC_NONE;
                    tb.SyncAfter = syncAfter;
                    tb.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
                    tb.AccessAfter = accessAfter;
                    tb.LayoutBefore = committedSub;
                    tb.LayoutAfter = assumedSub;
                    tb.pResource = pD3DResource;
                    tb.Subresources.IndexOrFirstMipLevel = sub;
                    tb.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
                    d3dBarriers.push_back(tb);
                }
            }
        }
    }

    bool hasFixups = !d3dBarriers.empty();

    if (m_pFixupCL && m_pFixupAllocator)
        m_pFixupCL->Reset(m_pFixupAllocator, nullptr);

    if (hasFixups && m_pFixupCL7)
    {
        D3D12_BARRIER_GROUP group = {};
        group.Type = D3D12_BARRIER_TYPE_TEXTURE;
        group.NumBarriers = static_cast<UINT>(d3dBarriers.size());
        group.pTextureBarriers = d3dBarriers.data();
        m_pFixupCL7->Barrier(1, &group);
    }

    if (m_pFixupCL)
        m_pFixupCL->Close();

    // Submit command lists via ExecuteCommandLists
    if (hasFixups)
    {
        ID3D12CommandList* pLists[] = { m_pFixupCL, m_pWorkCL };
        m_pCommandQueue->ExecuteCommandLists(2, pLists);
    }
    else
    {
        ID3D12CommandList* pLists[] = { m_pWorkCL };
        m_pCommandQueue->ExecuteCommandLists(1, pLists);
    }

    // Now update committed state on CSurface12 objects (after fixup barriers are computed).
    ComputeFinalLayouts();

    m_Dispatched = true;
}

//------------------------------------------------------------------------------------------------
// Compute Final Layouts - update committed state on CSurface12 objects
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::ComputeFinalLayouts()
{
    // Walk all tasks in order. The last task to declare usage for each (surface, subresource)
    // determines the final layout. Per-subresource usages update only the targeted subresource;
    // whole-resource usages (0xFFFFFFFF) set all subresources uniformly.
    std::unordered_map<ID3D12Resource*, std::pair<CSurface12*, SubresourceLayout>> finals;

    for (uint32_t i = 0; i < m_TaskCount; ++i)
    {
        for (const auto& tex : m_Tasks[i].TextureUsages)
        {
            ID3D12Resource* pD3DResource = tex.pSurface->GetD3DResource();
            auto it = finals.find(pD3DResource);

            if (tex.Subresources == 0xFFFFFFFF)
            {
                // Whole-resource usage: overwrite all subresources uniformly
                finals[pD3DResource] = { tex.pSurface,
                    SubresourceLayout(tex.RequiredLayout, tex.pSurface->m_NumSubresources) };
            }
            else
            {
                // Per-subresource usage: update only the targeted subresource
                if (it == finals.end())
                {
                    // First time seeing this resource — start from committed state
                    finals[pD3DResource] = { tex.pSurface, tex.pSurface->m_CurrentLayout };
                    it = finals.find(pD3DResource);
                }
                it->second.second.SetLayout(tex.Subresources, tex.RequiredLayout, tex.pSurface->m_NumSubresources);
            }
        }
    }

    for (const auto& [pD3DResource, pair] : finals)
    {
        auto [pSurface, layout] = pair;
        layout.TryCollapse();
        pSurface->m_CurrentLayout = layout;
    }
}

//------------------------------------------------------------------------------------------------
// Lifecycle
//------------------------------------------------------------------------------------------------

void CGpuTaskGraph::Reset(UINT64 fenceValue, UINT64 completedFenceValue)
{
    m_TaskCount = 0;
    m_Surfaces.clear();
    m_ExpectedInitialLayouts.clear();
    m_LastError.clear();

    if (m_pAllocatorPool)
    {
        m_pAllocatorPool->SwapAllocator(m_pWorkAllocator, fenceValue, completedFenceValue);
        m_pWorkAllocator->Reset();
    }
    if (m_pAllocatorPool)
    {
        m_pAllocatorPool->SwapAllocator(m_pFixupAllocator, fenceValue, completedFenceValue);
        m_pFixupAllocator->Reset();
    }

    if (m_pWorkCL && m_pWorkAllocator)
    {
        if (!m_Dispatched)
        {
            // Work CL was never dispatched — logic error in the caller's frame lifecycle.
            m_pWorkCL->Close();
            assert(false && "CGpuTaskGraph::Reset called without Dispatch — work CL was still open");
        }
        // Reset allocator + CL, then close. CL opened lazily before first InsertTask.
        m_pWorkAllocator->Reset();
        m_pWorkCL->Reset(m_pWorkAllocator, nullptr);
        m_pWorkCL->Close();
    }
    m_Dispatched = false;
}

void CGpuTaskGraph::ReleaseMemory()
{
    m_TaskCount = 0;
    m_Surfaces.clear();
    m_ExpectedInitialLayouts.clear();
    m_LastError.clear();
    m_Tasks.clear();
    m_Tasks.shrink_to_fit();
    m_ScratchBarriers.TextureBarriers.clear();
    m_ScratchBarriers.TextureBarriers.shrink_to_fit();
    m_ScratchBarriers.BufferBarriers.clear();
    m_ScratchBarriers.BufferBarriers.shrink_to_fit();
    m_pWorkCL.Release();
    m_pWorkCL7.Release();
    m_pWorkAllocator.Release();
    m_pFixupCL.Release();
    m_pFixupCL7.Release();
    m_pFixupAllocator.Release();
    m_pAllocatorPool = nullptr;
}

} // namespace Canvas
