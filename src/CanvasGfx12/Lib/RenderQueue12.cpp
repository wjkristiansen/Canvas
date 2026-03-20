//================================================================================================
// RenderQueue12 - D3D12 Command Queue and Rendering State Management
//
// THREADING MODEL:
// - NOT THREAD-SAFE: All methods must be called from a single thread
// - Concurrent access from multiple threads will cause undefined behavior
// - If multi-threaded rendering is required, use multiple RenderQueue instances
//================================================================================================

#include "pch.h"

#include "RenderQueue12.h"
#include "Buffer12.h"
#include "Surface12.h"
#include "SwapChain12.h"

//------------------------------------------------------------------------------------------------
CCommandAllocatorPool::CCommandAllocatorPool()
{
}

//------------------------------------------------------------------------------------------------
ID3D12CommandAllocator *CCommandAllocatorPool::Init(CDevice12 *pDevice, D3D12_COMMAND_LIST_TYPE Type, UINT NumAllocators)
{
    Canvas::CFunctionSentinel sentinel("CCommandAllocatorPool::Init", pDevice->GetLogger());
    
    for (UINT i = 0; i < NumAllocators; ++i)
    {
        CComPtr<ID3D12CommandAllocator> pAllocator;
        ID3D12Device *pD3DDevice = pDevice->GetD3DDevice();
        pD3DDevice->CreateCommandAllocator(Type, IID_PPV_ARGS(&pAllocator));
        CommandAllocators.push_back({ pAllocator, 0 });
    }

    AllocatorIndex = 0;
    return CommandAllocators[0].pCommandAllocator;
}

//------------------------------------------------------------------------------------------------
ID3D12CommandAllocator *CCommandAllocatorPool::RotateAllocators(CRenderQueue12 *pRenderQueue)
{
    CommandAllocators[AllocatorIndex].FenceValue = pRenderQueue->m_FenceValue;
    AllocatorIndex = (AllocatorIndex + 1) % CommandAllocators.size();

    if (CommandAllocators[AllocatorIndex].FenceValue > pRenderQueue->m_pFence->GetCompletedValue())
    {
        HANDLE hEvent = CreateEvent(nullptr, 0, 0, nullptr);
        pRenderQueue->m_pFence->SetEventOnCompletion(CommandAllocators[AllocatorIndex].FenceValue, hEvent);
        WaitForSingleObject(hEvent, INFINITE);
        CloseHandle(hEvent);
    }

    return CommandAllocators[AllocatorIndex].pCommandAllocator;
}

//------------------------------------------------------------------------------------------------
CRenderQueue12::CRenderQueue12(Canvas::XCanvas* pCanvas, CDevice12 *pDevice, PCSTR name) :
    TGfxElement(pCanvas),
    m_pDevice(pDevice)
{
    if (name != nullptr)
        SetName(name);
        
    CComPtr<ID3D12CommandQueue> pCQ;
    D3D12_COMMAND_QUEUE_DESC CQDesc;
    CQDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    CQDesc.NodeMask = 1;
    CQDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    CQDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    auto *pD3DDevice = pDevice->GetD3DDevice();

    Gem::ThrowGemError(ResultFromHRESULT(pD3DDevice->CreateCommandQueue(&CQDesc, IID_PPV_ARGS(&pCQ))));

    CComPtr<ID3D12CommandAllocator> pCA = m_CommandAllocatorPool.Init(pDevice, D3D12_COMMAND_LIST_TYPE_DIRECT, 4);

    CComPtr<ID3D12GraphicsCommandList> pCL;
    Gem::ThrowGemError(ResultFromHRESULT(pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pCA, nullptr, IID_PPV_ARGS(&pCL))));
    
    // Cache the ID3D12GraphicsCommandList7 interface once to avoid QueryInterface per barrier flush
    CComPtr<ID3D12GraphicsCommandList7> pCL7;
    if (SUCCEEDED(pCL->QueryInterface(IID_PPV_ARGS(&pCL7))))
    {
        m_pCommandList7 = pCL7;
    }

    CComPtr<ID3D12DescriptorHeap> pResDH;
    D3D12_DESCRIPTOR_HEAP_DESC DHDesc = {};
    DHDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    DHDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    DHDesc.NumDescriptors = NumShaderResourceDescriptors; // BUGBUG: This needs to be a well-known constant
    Gem::ThrowGemError(ResultFromHRESULT(pD3DDevice->CreateDescriptorHeap(&DHDesc, IID_PPV_ARGS(&pResDH))));

    CComPtr<ID3D12DescriptorHeap> pSamplerDH;
    DHDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    DHDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    DHDesc.NumDescriptors = NumSamplerDescriptors; // BUGBUG: This needs to be a well-known constant
    Gem::ThrowGemError(ResultFromHRESULT(pD3DDevice->CreateDescriptorHeap(&DHDesc, IID_PPV_ARGS(&pSamplerDH))));

    CComPtr<ID3D12DescriptorHeap> pRTVDH;
    DHDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    DHDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    DHDesc.NumDescriptors = NumRTVDescriptors; // BUGBUG: This needs to be a well-known constant
    Gem::ThrowGemError(ResultFromHRESULT(pD3DDevice->CreateDescriptorHeap(&DHDesc, IID_PPV_ARGS(&pRTVDH))));

    CComPtr<ID3D12DescriptorHeap> pDSVDH;
    DHDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    DHDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    DHDesc.NumDescriptors = NumDSVDescriptors; // BUGBUG: This needs to be a well-known constant
    Gem::ThrowGemError(ResultFromHRESULT(pD3DDevice->CreateDescriptorHeap(&DHDesc, IID_PPV_ARGS(&pDSVDH))));

    CComPtr<ID3D12Fence> pFence;
    Gem::ThrowGemError(ResultFromHRESULT(pD3DDevice->CreateFence(m_FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence))));

    // The default root signature uses the following parameters
    //  Root CBV (descriptor static)
    //  Root SRV (descriptor static)
    //  Root UAV (descriptor static)
    //  Root descriptor table

    // The default root descriptor table is layed out as follows:
    //  CBV[2] (data static)
    //  SRV[4] (data static)
    //  UAV[2] (descriptor static)

    std::vector<CD3DX12_DESCRIPTOR_RANGE1> DefaultDescriptorRanges(3);
    DefaultDescriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0);
    DefaultDescriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 2);
    DefaultDescriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 6);

    std::vector<CD3DX12_ROOT_PARAMETER1> DefaultRootParams(4);
    DefaultRootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    DefaultRootParams[1].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    DefaultRootParams[2].InitAsUnorderedAccessView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);
    DefaultRootParams[3].InitAsDescriptorTable(1, DefaultDescriptorRanges.data(), D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC DefaultRootSigDesc(4U, DefaultRootParams.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    CComPtr<ID3DBlob> pRSBlob;
    ThrowFailedHResult(D3D12SerializeVersionedRootSignature(&DefaultRootSigDesc, &pRSBlob, nullptr));

    CComPtr<ID3D12RootSignature> pDefaultRootSig;
    pD3DDevice->CreateRootSignature(1, pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(), IID_PPV_ARGS(&pDefaultRootSig));

    m_pShaderResourceDescriptorHeap.Attach(pResDH.Detach());
    m_pSamplerDescriptorHeap.Attach(pSamplerDH.Detach());
    m_pRTVDescriptorHeap.Attach(pRTVDH.Detach());
    m_pDSVDescriptorHeap.Attach(pDSVDH.Detach());
    m_pCommandAllocator.Attach(pCA.Detach());
    m_pCommandList.Attach(pCL.Detach());
    m_pCommandQueue.Attach(pCQ.Detach());
    m_pFence.Attach(pFence.Detach());

    // NOTE: Texture layout tracking is initialized after resource creation elsewhere
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::CreateSwapChain(HWND hWnd, bool Windowed, Canvas::XGfxSwapChain **ppSwapChain, Canvas::GfxFormat Format, UINT NumBuffers)
{
    Canvas::CFunctionSentinel sentinel("XGfxRenderQueue::CreateSwapChain", this->GetDevice()->GetLogger());
    try
    {
        // Create and register the swapchain
        DXGI_FORMAT dxgiFormat = CanvasFormatToDXGIFormat(Format);
        Gem::TGemPtr<CSwapChain12> pSwapChain;
        Gem::ThrowGemError(TGfxElement<CSwapChain12>::CreateAndRegister<CSwapChain12>(&pSwapChain, GetCanvas(), hWnd, Windowed, this, dxgiFormat, NumBuffers, nullptr));
        
        return pSwapChain->QueryInterface(ppSwapChain);
    }
    catch (Gem::GemError &e)
    {
        sentinel.SetResultCode(e.Result());
        return e.Result();
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CRenderQueue12::CopyBuffer(Canvas::XGfxBuffer *pDest, Canvas::XGfxBuffer *pSource)
{
    CBuffer12 *pDestBuffer = reinterpret_cast<CBuffer12 *>(pDest);
    CBuffer12 *pSourceBuffer = reinterpret_cast<CBuffer12 *>(pSource);
    
    EnsureTaskGraphActive();
    
    auto task = CreateGpuTask("CopyBuffer");
    DeclareGpuBufferUsage(task, pSourceBuffer->GetD3DResource(),
        D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE);
    DeclareGpuBufferUsage(task, pDestBuffer->GetD3DResource(),
        D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST);
    
    PrepareGpuTask(task);
    m_pCommandList->CopyResource(pDestBuffer->GetD3DResource(), pSourceBuffer->GetD3DResource());
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CRenderQueue12::ClearSurface(Canvas::XGfxSurface *pGfxSurface, const float Color[4])
{
    CSurface12 *pSurface = reinterpret_cast<CSurface12 *>(pGfxSurface);
    ID3D12Resource* pResource = pSurface->GetD3DResource();

    EnsureTaskGraphActive();
    
    auto task = CreateGpuTask("ClearSurface");
    DeclareGpuTextureUsage(task, pResource,
        D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        D3D12_BARRIER_SYNC_RENDER_TARGET,
        D3D12_BARRIER_ACCESS_RENDER_TARGET);
    
    PrepareGpuTask(task);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = CreateRenderTargetView(pSurface, 0, 0, 0);
    m_pCommandList->ClearRenderTargetView(rtv, Color, 0, nullptr);
    
    // If this surface belongs to a swap chain, mark that it was written this frame
    if (pSurface->m_pOwnerSwapChain != nullptr)
    {
        pSurface->m_pOwnerSwapChain->m_BackBufferModified = true;
    }
}

//------------------------------------------------------------------------------------------------
D3D12_CPU_DESCRIPTOR_HANDLE CRenderQueue12::CreateRenderTargetView(class CSurface12 *pSurface, UINT ArraySlice, UINT MipSlice, UINT PlaneSlice)
{
    ID3D12Device *pD3DDevice = m_pDevice->GetD3DDevice();
    UINT incSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT slot = m_NextRTVSlot;
    m_NextRTVSlot = (m_NextRTVSlot + 1) % NumRTVDescriptors;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    cpuHandle.ptr= m_pRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + (incSize * slot);
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtvDesc.Texture2DArray.ArraySize = 1;
    rtvDesc.Texture2DArray.FirstArraySlice = ArraySlice;
    rtvDesc.Texture2DArray.MipSlice = MipSlice;
    rtvDesc.Texture2DArray.PlaneSlice = PlaneSlice;
    ID3D12Resource *pD3DResource = pSurface->GetD3DResource();
    m_pDevice->GetD3DDevice()->CreateRenderTargetView(pD3DResource, &rtvDesc, cpuHandle);
    return cpuHandle;
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::Flush()
{
    // Update device committed state from task graph final layouts
    if (m_TaskGraphActive)
    {
        m_GpuTaskGraph.ComputeFinalLayouts();
        const auto& finalLayouts = m_GpuTaskGraph.GetFinalLayouts();
        for (const auto& [pResource, layoutState] : finalLayouts)
        {
            auto& committed = m_pDevice->m_TextureCurrentLayouts[pResource];
            if (layoutState.UniformLayout.has_value())
            {
                committed.uniformLayout = layoutState.UniformLayout.value();
                committed.perSubresourceLayouts.clear();
            }
            else
            {
                committed.uniformLayout.reset();
                committed.perSubresourceLayouts = layoutState.PerSubresourceLayouts;
            }
        }
    }
    
    // Close and submit
    ThrowFailedHResult(m_pCommandList->Close());
    ID3D12CommandList* pLists[] = { m_pCommandList };
    m_pCommandQueue->ExecuteCommandLists(1, pLists);
    
    // Signal fence
    m_pCommandQueue->Signal(m_pFence, ++m_FenceValue);
    
    // Reset for next frame
    m_pCommandAllocator = m_CommandAllocatorPool.RotateAllocators(this);
    m_pCommandAllocator->Reset();
    ThrowFailedHResult(m_pCommandList->Reset(m_pCommandAllocator, nullptr));
    m_GpuTaskGraph.Reset();
    m_TaskGraphActive = false;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::FlushAndPresent(Canvas::XGfxSwapChain *pSwapChain)
{
    try
    {
        CSwapChain12 *pIntSwapChain = reinterpret_cast<CSwapChain12 *>(pSwapChain);

        // Back buffer must be in COMMON for Present — add a barrier-only task
        if (pIntSwapChain->m_BackBufferModified)
        {
            ID3D12Resource* pBackBufferResource = pIntSwapChain->m_pSurface->GetD3DResource();

            EnsureTaskGraphActive();
            auto presentTask = CreateGpuTask("PresentTransition");
            DeclareGpuTextureUsage(presentTask, pBackBufferResource,
                D3D12_BARRIER_LAYOUT_COMMON,
                D3D12_BARRIER_SYNC_ALL,
                D3D12_BARRIER_ACCESS_COMMON);
            PrepareGpuTask(presentTask);  // Emits barrier, no commands to record
            
            pIntSwapChain->m_BackBufferModified = false;
        }

        // Finalize layouts, close, and submit the command list
        Flush();

        // Wait for frame latency and present
        pIntSwapChain->WaitForFrameLatency();
        pIntSwapChain->Present();

        // Clean up completed work every frame
        ProcessCompletedWork();
    }
    catch (Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::Uninitialize()
{
    Canvas::CFunctionSentinel sentinel("XGfxRenderQueue::Uninitialize", m_pDevice->GetLogger(), Canvas::LogLevel::Info);

    m_pCommandList->Close();

    // Wait for GPU to reach current fence value
    UINT64 completedValue = m_pFence->GetCompletedValue();
    if (completedValue < m_FenceValue)
    {
        HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (hEvent)
        {
            ThrowFailedHResult(m_pFence->SetEventOnCompletion(m_FenceValue, hEvent));
            WaitForSingleObject(hEvent, INFINITE);
            CloseHandle(hEvent);
        }
    }
}

//------------------------------------------------------------------------------------------------
// GPU workload management
//------------------------------------------------------------------------------------------------


//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
void CRenderQueue12::CreateGpuSyncPoint(UINT64 fenceValue)
{
    // Signal the fence on the GPU timeline
    m_pCommandQueue->Signal(m_pFence, fenceValue);
    
    // Store sync point for later wait / cleanup
    m_GpuSyncPoints[fenceValue] = GpuSyncPoint{ fenceValue, m_pFence };
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::WaitForGpuFence(UINT64 fenceValue)
{
    // Wait on CPU for GPU fence completion
    if (m_pFence->GetCompletedValue() < fenceValue)
    {
        HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        m_pFence->SetEventOnCompletion(fenceValue, hEvent);
        WaitForSingleObject(hEvent, INFINITE);
        CloseHandle(hEvent);
    }
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::RetireUploadAllocation(const Canvas::GfxSuballocation& suballocation)
{
    // Defer release until the GPU completes the next submit (current fence value + 1).
    // ProcessCompletedWork will free the suballocation once the fence advances past this value.
    m_PendingUploadRetirements.push_back({ suballocation, m_FenceValue + 1 });
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::RecordCommands(
    const ResourceUsages& resourceUsages,
    std::function<void(ID3D12GraphicsCommandList*)> recordFunc)
{
    // Validate resource usage patterns (when diagnostics enabled)
    if (!ValidateResourceUsageNoWriteConflicts(resourceUsages))
    {
        throw Gem::GemError(Gem::Result::InvalidArg);
    }
    
    EnsureTaskGraphActive();
    
    // Create a task and translate ResourceUsages to GpuTask declarations
    auto task = CreateGpuTask("RecordCommands");
    
    for (const auto& texUsage : resourceUsages.TextureUsages)
    {
        if (!texUsage.IsValid()) continue;
        DeclareGpuTextureUsage(task, texUsage.pResource,
            texUsage.RequiredLayout,
            texUsage.SyncForUsage,
            texUsage.AccessForUsage,
            texUsage.Subresources);
    }
    
    for (const auto& bufUsage : resourceUsages.BufferUsages)
    {
        if (!bufUsage.IsValid()) continue;
        DeclareGpuBufferUsage(task, bufUsage.pResource,
            bufUsage.SyncForUsage,
            bufUsage.AccessForUsage,
            bufUsage.Offset,
            bufUsage.Size);
    }
    
    PrepareGpuTask(task);
    recordFunc(m_pCommandList);
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::PresentSwapChain(Canvas::XGfxSwapChain* pSwapChain)
{
    CSwapChain12* pIntSwapChain = reinterpret_cast<CSwapChain12*>(pSwapChain);
    Gem::ThrowGemError(pIntSwapChain->Present());
}

//------------------------------------------------------------------------------------------------
bool CRenderQueue12::ValidateResourceUsageNoWriteConflicts(const ResourceUsages& resourceUsages) const
{
    return resourceUsages.IsValidNoWriteConflicts();
}

//------------------------------------------------------------------------------------------------
CRenderQueue12::ResourceStateSnapshot CRenderQueue12::GetResourceState(ID3D12Resource* pResource) const
{
    ResourceStateSnapshot snapshot;
    
    // Read from device committed state (the ground truth for resource layouts)
    auto it = m_pDevice->m_TextureCurrentLayouts.find(pResource);
    if (it != m_pDevice->m_TextureCurrentLayouts.end())
    {
        const auto& layoutState = it->second;
        if (layoutState.uniformLayout.has_value())
        {
            snapshot.UniformLayout = layoutState.uniformLayout.value();
        }
        else
        {
            snapshot.PerSubresourceLayouts = layoutState.perSubresourceLayouts;
        }
    }
    
    return snapshot;
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::ProcessCompletedWork()
{
    UINT64 completedValue = m_pFence->GetCompletedValue();
    
    // Release deferred host-write suballocations whose fence has completed
    for (auto it = m_PendingUploadRetirements.begin(); it != m_PendingUploadRetirements.end();)
    {
        if (it->FenceValue <= completedValue)
        {
            if (m_pDevice)
            {
                m_pDevice->FreeHostWriteRegion(const_cast<Canvas::GfxSuballocation&>(it->Suballocation));
            }
            it = m_PendingUploadRetirements.erase(it);
        }
        else
        {
            ++it;
        }
    }
    
    // Clean up completed sync points
    for (auto it = m_GpuSyncPoints.begin(); it != m_GpuSyncPoints.end();)
    {
        if (it->second.FenceValue <= completedValue)
        {
            it = m_GpuSyncPoints.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

//================================================================================================
// GPU Task Graph API Implementation
//================================================================================================

//------------------------------------------------------------------------------------------------
void CRenderQueue12::EnsureTaskGraphActive()
{
    if (!m_TaskGraphActive)
    {
        BeginTaskGraph();
        m_TaskGraphActive = true;
    }
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::BeginTaskGraph()
{
    m_GpuTaskGraph.Reset();
    
    // Populate initial layouts from device committed state
    for (const auto& [pResource, layoutState] : m_pDevice->m_TextureCurrentLayouts)
    {
        if (layoutState.uniformLayout.has_value())
        {
            m_GpuTaskGraph.SetInitialLayout(pResource, layoutState.uniformLayout.value());
        }
        else
        {
            for (const auto& [sub, layout] : layoutState.perSubresourceLayouts)
            {
                m_GpuTaskGraph.SetInitialLayout(pResource, layout, sub);
            }
        }
    }
}

//------------------------------------------------------------------------------------------------
Canvas::GpuTaskHandle CRenderQueue12::CreateGpuTask(const char* name)
{
    return m_GpuTaskGraph.CreateTask(name);
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::DeclareGpuTextureUsage(
    Canvas::GpuTaskHandle task,
    ID3D12Resource* pResource,
    D3D12_BARRIER_LAYOUT requiredLayout,
    D3D12_BARRIER_SYNC sync,
    D3D12_BARRIER_ACCESS access,
    UINT subresources)
{
    m_GpuTaskGraph.DeclareTextureUsage(task, pResource, requiredLayout, sync, access, subresources);
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::DeclareGpuBufferUsage(
    Canvas::GpuTaskHandle task,
    ID3D12Resource* pResource,
    D3D12_BARRIER_SYNC sync,
    D3D12_BARRIER_ACCESS access,
    UINT64 offset,
    UINT64 size)
{
    m_GpuTaskGraph.DeclareBufferUsage(task, pResource, sync, access, offset, size);
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::PrepareGpuTask(Canvas::GpuTaskHandle task)
{
    Canvas::TaskBarriers barriers = m_GpuTaskGraph.PrepareTask(task);
    EmitBarriers(barriers);
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::EmitBarriers(const Canvas::TaskBarriers& barriers)
{
    std::vector<D3D12_TEXTURE_BARRIER> d3dTexBarriers;
    std::vector<D3D12_BUFFER_BARRIER> d3dBufBarriers;
    
    for (const auto& tb : barriers.TextureBarriers)
    {
        D3D12_TEXTURE_BARRIER d3dBarrier = {};
        d3dBarrier.SyncBefore = tb.SyncBefore;
        d3dBarrier.SyncAfter = tb.SyncAfter;
        d3dBarrier.AccessBefore = tb.AccessBefore;
        d3dBarrier.AccessAfter = tb.AccessAfter;
        d3dBarrier.LayoutBefore = tb.LayoutBefore;
        d3dBarrier.LayoutAfter = tb.LayoutAfter;
        d3dBarrier.pResource = tb.pResource;
        d3dBarrier.Subresources.IndexOrFirstMipLevel = tb.Subresources;
        d3dBarrier.Flags = tb.Flags;
        d3dTexBarriers.push_back(d3dBarrier);
    }
    
    for (const auto& bb : barriers.BufferBarriers)
    {
        D3D12_BUFFER_BARRIER d3dBarrier = {};
        d3dBarrier.SyncBefore = bb.SyncBefore;
        d3dBarrier.SyncAfter = bb.SyncAfter;
        d3dBarrier.AccessBefore = bb.AccessBefore;
        d3dBarrier.AccessAfter = bb.AccessAfter;
        d3dBarrier.pResource = bb.pResource;
        d3dBarrier.Offset = bb.Offset;
        d3dBarrier.Size = bb.Size;
        d3dBufBarriers.push_back(d3dBarrier);
    }
    
    std::vector<D3D12_BARRIER_GROUP> groups;
    if (!d3dTexBarriers.empty())
    {
        D3D12_BARRIER_GROUP group = {};
        group.Type = D3D12_BARRIER_TYPE_TEXTURE;
        group.NumBarriers = static_cast<UINT>(d3dTexBarriers.size());
        group.pTextureBarriers = d3dTexBarriers.data();
        groups.push_back(group);
    }
    if (!d3dBufBarriers.empty())
    {
        D3D12_BARRIER_GROUP group = {};
        group.Type = D3D12_BARRIER_TYPE_BUFFER;
        group.NumBarriers = static_cast<UINT>(d3dBufBarriers.size());
        group.pBufferBarriers = d3dBufBarriers.data();
        groups.push_back(group);
    }
    
    if (!groups.empty() && m_pCommandList7)
    {
        m_pCommandList7->Barrier(static_cast<UINT>(groups.size()), groups.data());
    }
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::AddGpuTaskDependency(Canvas::GpuTaskHandle task, Canvas::GpuTaskHandle dependency)
{
    m_GpuTaskGraph.AddExplicitDependency(task, dependency);
}
