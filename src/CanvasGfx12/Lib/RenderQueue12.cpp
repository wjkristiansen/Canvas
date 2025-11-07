//================================================================================================
// RenderQueue12 - D3D12 Command Queue and Rendering State Management
//
// THREADING MODEL:
// - NOT THREAD-SAFE: All methods must be called from a single thread
// - Concurrent access from multiple threads will cause undefined behavior
// - If multi-threaded rendering is required, use multiple RenderQueue instances
// - TaskScheduler handles task dependency ordering with immediate inline execution
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
    m_pDevice(pDevice),
    m_TaskScheduler(1024 * 1024)  // 1MB initial ring buffer for task allocation
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
    
    // Ensure buffers have proper access, automatically detecting hazards
    auto srcAccessTask = EnsureBufferAccess(
        pSourceBuffer->GetD3DResource(),
        D3D12_BARRIER_SYNC_COPY,  // This copy will sync at COPY scope
        D3D12_BARRIER_ACCESS_COPY_SOURCE  // This copy will read as COPY_SOURCE
    );
    
    auto dstAccessTask = EnsureBufferAccess(
        pDestBuffer->GetD3DResource(),
        D3D12_BARRIER_SYNC_COPY,
        D3D12_BARRIER_ACCESS_COPY_DEST  // This copy will write as COPY_DEST
    );
    
    // Schedule copy command - depends on both access tasks
    Canvas::TaskID copyDep = (srcAccessTask != Canvas::InvalidTaskID) ? srcAccessTask : Canvas::InvalidTaskID;
    auto copyTask = ScheduleCommandListRecording(
        [pDestBuffer, pSourceBuffer](ID3D12GraphicsCommandList* cmdList) {
            cmdList->CopyResource(pDestBuffer->GetD3DResource(), pSourceBuffer->GetD3DResource());
        },
        copyDep
    );
    
    // Add dependency on dest access too
    if (dstAccessTask != Canvas::InvalidTaskID && dstAccessTask != copyDep)
    {
        m_TaskScheduler.AddDependency(copyTask, dstAccessTask);
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CRenderQueue12::ClearSurface(Canvas::XGfxSurface *pGfxSurface, const float Color[4])
{
    CSurface12 *pSurface = reinterpret_cast<CSurface12 *>(pGfxSurface);
    
    ID3D12Resource* pResource = pSurface->GetD3DResource();
    auto it = m_ResourceStates.find(pResource);
    
    // Check if layout transition needed
    D3D12_BARRIER_LAYOUT currentLayout = (it != m_ResourceStates.end()) 
        ? it->second.CurrentLayout 
        : D3D12_BARRIER_LAYOUT_COMMON;
    
    if (currentLayout != D3D12_BARRIER_LAYOUT_RENDER_TARGET)
    {
        // Need layout transition
        D3D12_TEXTURE_BARRIER barrier = {};
        barrier.SyncBefore = (it != m_ResourceStates.end()) ? it->second.LastSyncInCommandList : D3D12_BARRIER_SYNC_NONE;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
        barrier.AccessBefore = (it != m_ResourceStates.end()) ? it->second.LastAccessInCommandList : D3D12_BARRIER_ACCESS_NO_ACCESS;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
        barrier.LayoutBefore = currentLayout;
        barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        barrier.pResource = pResource;
        barrier.Subresources.IndexOrFirstMipLevel = 0xffffffff;
        barrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
        
        D3D12_BARRIER_GROUP barrierGroup = {};
        barrierGroup.Type = D3D12_BARRIER_TYPE_TEXTURE;
        barrierGroup.NumBarriers = 1;
        barrierGroup.pTextureBarriers = &barrier;
        
        if (m_pCommandList7)
        {
            m_pCommandList7->Barrier(1, &barrierGroup);
        }
    }
    
    // Execute clear
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = CreateRenderTargetView(pSurface, 0, 0, 0);
    m_pCommandList->ClearRenderTargetView(rtv, Color, 0, nullptr);
    
    // Update resource state
    ResourceState& state = m_ResourceStates[pResource];
    state.CurrentLayout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    state.LastSyncInCommandList = D3D12_BARRIER_SYNC_RENDER_TARGET;
    state.LastAccessInCommandList = D3D12_BARRIER_ACCESS_RENDER_TARGET;
    state.LastUsageTask = Canvas::InvalidTaskID;
    if (!state.UsedInCurrentCommandList)
    {
        state.UsedInCurrentCommandList = true;
        m_UsedResourcesDuringCL.push_back(pResource);
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
Gem::Result CRenderQueue12::FlushImpl()
{
    try
    {
        // Schedule command list submission
        auto submitTask = SubmitCommandList();
        
        // Wait for submission to complete (synchronous flush)
        WaitForGpuFence(m_FenceValue, submitTask);
        
        return Gem::Result::Success;
    }
    catch (_com_error &e)
    {
        return ResultFromHRESULT(e.Error());
    }
}

//------------------------------------------------------------------------------------------------
Gem::Result CRenderQueue12::Flush()
{
    // NOTE: Removed CFunctionSentinel to eliminate per-frame logging overhead in hot path
    try
    {
        Gem::ThrowGemError(FlushImpl());

        // Reset command list and per-command-list tracking for next frame
        ThrowFailedHResult(m_pCommandList->Reset(m_pCommandAllocator, nullptr));
        
        // Clear pending barriers on command list reset
        m_PendingTextureBarriers.clear();
        m_PendingBufferBarriers.clear();
        m_PendingGlobalBarriers.clear();
        
        // Reset per-command-list tracking only for resources actually used
        for (ID3D12Resource* resource : m_UsedResourcesDuringCL)
        {
            auto it = m_ResourceStates.find(resource);
            if (it != m_ResourceStates.end())
            {
                it->second.UsedInCurrentCommandList = false;
                it->second.LastSyncInCommandList = D3D12_BARRIER_SYNC_NONE;
                it->second.LastAccessInCommandList = D3D12_BARRIER_ACCESS_NO_ACCESS;
            }
        }
        m_UsedResourcesDuringCL.clear();
        
        // Process completed tasks to reclaim memory (throttled - only every N frames)
        // NOTE: RetireCompletedTasks is expensive (sorts task map), don't call every frame
        if (++m_FramesSinceLastRetire >= 60)
        {
            ProcessCompletedWork();
            m_FramesSinceLastRetire = 0;
        }
    }
    catch (_com_error &e)
    {
        return ResultFromHRESULT(e.Error());
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::FlushAndPresent(Canvas::XGfxSwapChain *pSwapChain)
{
    // NOTE: Removed CFunctionSentinel to eliminate per-frame logging overhead in hot path
    try
    {
        CSwapChain12 *pIntSwapChain = reinterpret_cast<CSwapChain12 *>(pSwapChain);
        
        // Pace the CPU using DXGI frame latency waitable object if available
        // This prevents CPU from getting too far ahead of GPU
        pIntSwapChain->WaitForFrameLatency();
        
        // Build task graph for present operation:
        // 1. Prepare back buffer for present (transition to PRESENT layout)
        // 2. Submit command list to GPU
        // 3. Execute present operation
        // 4. Reset command list for next frame
        
        Canvas::TaskID prepareTask = PrepareForPresent(pSwapChain, Canvas::InvalidTaskID);
        Canvas::TaskID submitTask = SubmitCommandList(prepareTask);
        Canvas::TaskID presentTask = SchedulePresent(pSwapChain, submitTask);
        
        // Store present task so next frame can depend on it
        pIntSwapChain->m_LastFramePresentTask = presentTask;
        
        // Schedule command list reset task (depends on submit completing)
        Canvas::TaskID deps[1] = { submitTask };
        m_TaskScheduler.AllocateAndScheduleTypedTask(
            deps,
            1u,
            [](Canvas::TaskID id, Canvas::TaskScheduler& sched, CRenderQueue12* pQueue)
            {
                // Rotate command allocators (waits for GPU if needed)
                pQueue->m_pCommandAllocator = pQueue->m_CommandAllocatorPool.RotateAllocators(pQueue);
                pQueue->m_pCommandAllocator->Reset();
                
                // Reset command list
                ThrowFailedHResult(pQueue->m_pCommandList->Reset(pQueue->m_pCommandAllocator, nullptr));
                
                // Clear pending barriers on command list reset
                pQueue->m_PendingTextureBarriers.clear();
                pQueue->m_PendingBufferBarriers.clear();
                pQueue->m_PendingGlobalBarriers.clear();
                
                // Reset per-command-list tracking only for resources actually used
                for (ID3D12Resource* resource : pQueue->m_UsedResourcesDuringCL)
                {
                    auto it = pQueue->m_ResourceStates.find(resource);
                    if (it != pQueue->m_ResourceStates.end())
                    {
                        it->second.UsedInCurrentCommandList = false;
                        it->second.LastSyncInCommandList = D3D12_BARRIER_SYNC_NONE;
                        it->second.LastAccessInCommandList = D3D12_BARRIER_ACCESS_NO_ACCESS;
                    }
                }
                pQueue->m_UsedResourcesDuringCL.clear();
                
                sched.CompleteTask(id);
            },
            this
        );
        
        // Process completed tasks to reclaim memory (throttled - only every N frames)
        // NOTE: RetireCompletedTasks is expensive (sorts task map), don't call every frame
        if (++m_FramesSinceLastRetire >= 60)
        {
            ProcessCompletedWork();
            m_FramesSinceLastRetire = 0;
        }
    }
    catch (Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::Wait()
{
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
    
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::Uninitialize()
{
    Canvas::CFunctionSentinel sentinel("XGfxRenderQueue::Uninitialize", m_pDevice->GetLogger(), Canvas::LogLevel::Info);

    m_pCommandList->Close();

    Wait();
}

//------------------------------------------------------------------------------------------------
// Task-based GPU workload management implementation
//------------------------------------------------------------------------------------------------

Canvas::TaskID CRenderQueue12::ScheduleCommandListRecording(
    std::function<void(ID3D12GraphicsCommandList*)> recordFunc,
    Canvas::TaskID dependsOn)
{
    // Allocate and schedule task with optional single dependency
    Canvas::TaskID deps[1] = { dependsOn };
    auto taskId = m_TaskScheduler.AllocateAndScheduleTypedTask(
        (dependsOn != Canvas::InvalidTaskID) ? deps : nullptr,
        (dependsOn != Canvas::InvalidTaskID) ? 1u : 0u,
        [](Canvas::TaskID id, Canvas::TaskScheduler& sched, 
           CRenderQueue12* pQueue, std::function<void(ID3D12GraphicsCommandList*)> func)
        {
            // Flush any pending barriers before recording the command
            pQueue->FlushPendingBarriers();
            
            // Execute recording function
            func(pQueue->m_pCommandList);
            
            // Complete task synchronously
            sched.CompleteTask(id);
        },
        this,
        std::move(recordFunc)
    );
    
    return taskId;
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::EnsureTextureLayout(
    ID3D12Resource* pResource,
    D3D12_BARRIER_SYNC syncAfter,
    D3D12_BARRIER_ACCESS accessAfter,
    D3D12_BARRIER_LAYOUT requiredLayout,
    UINT subresources,
    D3D12_TEXTURE_BARRIER_FLAGS flags,
    Canvas::TaskID dependsOn)
{
    // Look up resource state
    auto it = m_ResourceStates.find(pResource);
    
    bool needsLayoutTransition = false;
    bool needsHazardBarrier = false;
    D3D12_BARRIER_LAYOUT layoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
    D3D12_BARRIER_SYNC syncBefore = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_ACCESS accessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    Canvas::TaskID lastUsageTask = Canvas::InvalidTaskID;
    
    if (it != m_ResourceStates.end())
    {
        const auto& state = it->second;
        layoutBefore = state.CurrentLayout;
        lastUsageTask = state.LastUsageTask;
        
        // Check if layout transition is needed (persistent state)
        needsLayoutTransition = (layoutBefore != requiredLayout);
        
        // Check for hazards within current command list
        if (state.UsedInCurrentCommandList)
        {
            syncBefore = state.LastSyncInCommandList;
            accessBefore = state.LastAccessInCommandList;
            
            // Detect GPU hazards:
            // - Write-after-read (WAR): Previous read, current write
            // - Read-after-write (RAW): Previous write, current read
            // - Write-after-write (WAW): Previous write, current write
            
            bool previousWasWrite = (accessBefore & D3D12_BARRIER_ACCESS_RENDER_TARGET) ||
                                   (accessBefore & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS) ||
                                   (accessBefore & D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE) ||
                                   (accessBefore & D3D12_BARRIER_ACCESS_COPY_DEST);
            
            bool currentIsWrite = (accessAfter & D3D12_BARRIER_ACCESS_RENDER_TARGET) ||
                                 (accessAfter & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS) ||
                                 (accessAfter & D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE) ||
                                 (accessAfter & D3D12_BARRIER_ACCESS_COPY_DEST);
            
            // Need barrier if there's any write involved (WAR, RAW, or WAW)
            needsHazardBarrier = previousWasWrite || currentIsWrite;
        }
    }
    else
    {
        // First use - assume resource starts in COMMON layout with no previous access
        needsLayoutTransition = (requiredLayout != D3D12_BARRIER_LAYOUT_COMMON);
        layoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
        syncBefore = D3D12_BARRIER_SYNC_NONE;
        accessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    }
    
    Canvas::TaskID barrierTaskId = Canvas::InvalidTaskID;
    
    // Insert barrier if layout transition or hazard detection requires it
    if (needsLayoutTransition || needsHazardBarrier)
    {
        TextureBarrier barrier;
        barrier.pResource = pResource;
        barrier.SyncBefore = syncBefore;
        barrier.SyncAfter = syncAfter;
        barrier.AccessBefore = accessBefore;
        barrier.AccessAfter = accessAfter;
        barrier.LayoutBefore = layoutBefore;
        barrier.LayoutAfter = requiredLayout;
        barrier.Subresources = subresources;
        barrier.Flags = flags;
        
        // Barrier depends on both the last usage and any explicit dependency
        Canvas::TaskID barrierDependency = dependsOn;
        if (lastUsageTask != Canvas::InvalidTaskID)
        {
            barrierDependency = lastUsageTask;
        }
        
        barrierTaskId = ScheduleTextureBarrier(barrier, barrierDependency);
        
        // If there was an explicit dependency different from last usage, add it
        if (dependsOn != Canvas::InvalidTaskID && dependsOn != lastUsageTask)
        {
            m_TaskScheduler.AddDependency(barrierTaskId, dependsOn);
        }
        
        // Update tracked state
        ResourceState& state = m_ResourceStates[pResource];
        state.CurrentLayout = requiredLayout;
        state.LastSyncInCommandList = syncAfter;
        state.LastAccessInCommandList = accessAfter;
        state.LastUsageTask = barrierTaskId;
        if (!state.UsedInCurrentCommandList)
        {
            state.UsedInCurrentCommandList = true;
            m_UsedResourcesDuringCL.push_back(pResource);
        }
    }
    else
    {
        // No barrier needed, but update command-list-local tracking
        Canvas::TaskID usageTask = (dependsOn != Canvas::InvalidTaskID) ? dependsOn : lastUsageTask;
        
        if (it != m_ResourceStates.end())
        {
            it->second.LastSyncInCommandList = syncAfter;
            it->second.LastAccessInCommandList = accessAfter;
            if (!it->second.UsedInCurrentCommandList)
            {
                it->second.UsedInCurrentCommandList = true;
                m_UsedResourcesDuringCL.push_back(pResource);
            }
            if (usageTask != Canvas::InvalidTaskID)
            {
                it->second.LastUsageTask = usageTask;
            }
        }
        else
        {
            ResourceState& state = m_ResourceStates[pResource];
            state.CurrentLayout = layoutBefore;
            state.LastSyncInCommandList = syncAfter;
            state.LastAccessInCommandList = accessAfter;
            state.LastUsageTask = usageTask;
            state.UsedInCurrentCommandList = true;
            m_UsedResourcesDuringCL.push_back(pResource);
        }
        
        barrierTaskId = lastUsageTask != Canvas::InvalidTaskID ? lastUsageTask : dependsOn;
    }
    
    return barrierTaskId;
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::EnsureBufferAccess(
    ID3D12Resource* pResource,
    D3D12_BARRIER_SYNC syncAfter,
    D3D12_BARRIER_ACCESS accessAfter,
    UINT64 offset,
    UINT64 size,
    Canvas::TaskID dependsOn)
{
    // Look up resource state (buffers don't have layout, only track access/sync)
    auto it = m_ResourceStates.find(pResource);
    
    bool needsHazardBarrier = false;
    D3D12_BARRIER_SYNC syncBefore = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_ACCESS accessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    Canvas::TaskID lastUsageTask = Canvas::InvalidTaskID;
    
    if (it != m_ResourceStates.end())
    {
        const auto& state = it->second;
        lastUsageTask = state.LastUsageTask;
        
        // Check for hazards within current command list
        if (state.UsedInCurrentCommandList)
        {
            syncBefore = state.LastSyncInCommandList;
            accessBefore = state.LastAccessInCommandList;
            
            // Detect GPU hazards for buffers
            bool previousWasWrite = (accessBefore & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS) ||
                                   (accessBefore & D3D12_BARRIER_ACCESS_COPY_DEST);
            
            bool currentIsWrite = (accessAfter & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS) ||
                                 (accessAfter & D3D12_BARRIER_ACCESS_COPY_DEST);
            
            // Need barrier if there's any write involved (WAR, RAW, or WAW)
            needsHazardBarrier = previousWasWrite || currentIsWrite;
        }
    }
    else
    {
        // First use - no previous access
        syncBefore = D3D12_BARRIER_SYNC_NONE;
        accessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    }
    
    Canvas::TaskID barrierTaskId = Canvas::InvalidTaskID;
    
    if (needsHazardBarrier)
    {
        // Insert barrier to prevent GPU hazard
        barrierTaskId = ScheduleBufferBarrier(
            pResource,
            syncBefore,
            syncAfter,
            accessBefore,
            accessAfter,
            offset,
            size,
            lastUsageTask
        );
        
        // If there was an explicit dependency different from last usage, add it
        if (dependsOn != Canvas::InvalidTaskID && dependsOn != lastUsageTask)
        {
            m_TaskScheduler.AddDependency(barrierTaskId, dependsOn);
        }
        
        // Update tracked state (buffers use UNDEFINED for layout field)
        ResourceState& state = m_ResourceStates[pResource];
        state.CurrentLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;
        state.LastSyncInCommandList = syncAfter;
        state.LastAccessInCommandList = accessAfter;
        state.LastUsageTask = barrierTaskId;
        if (!state.UsedInCurrentCommandList)
        {
            state.UsedInCurrentCommandList = true;
            m_UsedResourcesDuringCL.push_back(pResource);
        }
    }
    else
    {
        // No barrier needed, but update command-list-local tracking
        Canvas::TaskID usageTask = (dependsOn != Canvas::InvalidTaskID) ? dependsOn : lastUsageTask;
        
        if (it != m_ResourceStates.end())
        {
            it->second.LastSyncInCommandList = syncAfter;
            it->second.LastAccessInCommandList = accessAfter;
            if (!it->second.UsedInCurrentCommandList)
            {
                it->second.UsedInCurrentCommandList = true;
                m_UsedResourcesDuringCL.push_back(pResource);
            }
            if (usageTask != Canvas::InvalidTaskID)
            {
                it->second.LastUsageTask = usageTask;
            }
        }
        else
        {
            ResourceState& state = m_ResourceStates[pResource];
            state.CurrentLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;
            state.LastSyncInCommandList = syncAfter;
            state.LastAccessInCommandList = accessAfter;
            state.LastUsageTask = usageTask;
            state.UsedInCurrentCommandList = true;
            m_UsedResourcesDuringCL.push_back(pResource);
        }
        
        barrierTaskId = lastUsageTask != Canvas::InvalidTaskID ? lastUsageTask : dependsOn;
    }
    
    return barrierTaskId;
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::ScheduleBufferBarrier(
    ID3D12Resource* pResource,
    D3D12_BARRIER_SYNC syncBefore,
    D3D12_BARRIER_SYNC syncAfter,
    D3D12_BARRIER_ACCESS accessBefore,
    D3D12_BARRIER_ACCESS accessAfter,
    UINT64 offset,
    UINT64 size,
    Canvas::TaskID dependsOn)
{
    // Accumulate barrier for batched recording
    // NOTE: Caller must hold m_mutex
    BufferBarrier barrier;
    barrier.pResource = pResource;
    barrier.SyncBefore = syncBefore;
    barrier.SyncAfter = syncAfter;
    barrier.AccessBefore = accessBefore;
    barrier.AccessAfter = accessAfter;
    barrier.Offset = offset;
    barrier.Size = size;
    
    m_PendingBufferBarriers.push_back(barrier);
    
    // Update resource state tracking
    ResourceState& state = m_ResourceStates[pResource];
    state.CurrentLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;  // Buffers don't have layout
    state.LastSyncInCommandList = syncAfter;
    state.LastAccessInCommandList = accessAfter;
    state.LastUsageTask = dependsOn;
    state.UsedInCurrentCommandList = true;
    
    return dependsOn;
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::ScheduleTextureBarrier(
    const TextureBarrier& barrier,
    Canvas::TaskID dependsOn)
{
    // Accumulate barrier for batched recording
    // NOTE: Caller must hold m_mutex
    m_PendingTextureBarriers.push_back(barrier);
    
    // Return dependency - the barrier will be flushed before next actual command
    return dependsOn;
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::ScheduleGlobalBarrier(
    const GlobalBarrier& barrier,
    Canvas::TaskID dependsOn)
{
    // Accumulate barrier for batched recording
    // NOTE: Caller must hold m_mutex
    m_PendingGlobalBarriers.push_back(barrier);
    
    // Return dependency - the barrier will be flushed before next actual command
    return dependsOn;
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::ScheduleBarrierGroup(
    const std::vector<TextureBarrier>& textureBarriers,
    const std::vector<BufferBarrier>& bufferBarriers,
    const std::vector<GlobalBarrier>& globalBarriers,
    Canvas::TaskID dependsOn)
{
    // Allocate and schedule task with barrier group payload
    Canvas::TaskID deps[1] = { dependsOn };
    auto taskId = m_TaskScheduler.AllocateAndScheduleTypedTask(
        (dependsOn != Canvas::InvalidTaskID) ? deps : nullptr,
        (dependsOn != Canvas::InvalidTaskID) ? 1u : 0u,
        [](Canvas::TaskID id, Canvas::TaskScheduler& sched, 
           CRenderQueue12* pQueue,
           std::vector<TextureBarrier> texBarriers,
           std::vector<BufferBarrier> bufBarriers,
           std::vector<GlobalBarrier> globBarriers)
        {
            std::vector<D3D12_TEXTURE_BARRIER> d3dTexBarriers;
            std::vector<D3D12_BUFFER_BARRIER> d3dBufBarriers;
            std::vector<D3D12_GLOBAL_BARRIER> d3dGlobBarriers;
            
            // Convert texture barriers
            for (const auto& barrier : texBarriers)
            {
                D3D12_TEXTURE_BARRIER d3dBarrier = {};
                d3dBarrier.SyncBefore = barrier.SyncBefore;
                d3dBarrier.SyncAfter = barrier.SyncAfter;
                d3dBarrier.AccessBefore = barrier.AccessBefore;
                d3dBarrier.AccessAfter = barrier.AccessAfter;
                d3dBarrier.LayoutBefore = barrier.LayoutBefore;
                d3dBarrier.LayoutAfter = barrier.LayoutAfter;
                d3dBarrier.pResource = barrier.pResource;
                d3dBarrier.Subresources.IndexOrFirstMipLevel = barrier.Subresources;
                d3dBarrier.Flags = barrier.Flags;
                d3dTexBarriers.push_back(d3dBarrier);
            }
            
            // Convert buffer barriers
            for (const auto& barrier : bufBarriers)
            {
                D3D12_BUFFER_BARRIER d3dBarrier = {};
                d3dBarrier.SyncBefore = barrier.SyncBefore;
                d3dBarrier.SyncAfter = barrier.SyncAfter;
                d3dBarrier.AccessBefore = barrier.AccessBefore;
                d3dBarrier.AccessAfter = barrier.AccessAfter;
                d3dBarrier.pResource = barrier.pResource;
                d3dBarrier.Offset = barrier.Offset;
                d3dBarrier.Size = barrier.Size;
                d3dBufBarriers.push_back(d3dBarrier);
            }
            
            // Convert global barriers
            for (const auto& barrier : globBarriers)
            {
                D3D12_GLOBAL_BARRIER d3dBarrier = {};
                d3dBarrier.SyncBefore = barrier.SyncBefore;
                d3dBarrier.SyncAfter = barrier.SyncAfter;
                d3dBarrier.AccessBefore = barrier.AccessBefore;
                d3dBarrier.AccessAfter = barrier.AccessAfter;
                d3dGlobBarriers.push_back(d3dBarrier);
            }
            
            // Build barrier groups
            std::vector<D3D12_BARRIER_GROUP> barrierGroups;
            
            if (!d3dTexBarriers.empty())
            {
                D3D12_BARRIER_GROUP group = {};
                group.Type = D3D12_BARRIER_TYPE_TEXTURE;
                group.NumBarriers = static_cast<UINT>(d3dTexBarriers.size());
                group.pTextureBarriers = d3dTexBarriers.data();
                barrierGroups.push_back(group);
            }
            
            if (!d3dBufBarriers.empty())
            {
                D3D12_BARRIER_GROUP group = {};
                group.Type = D3D12_BARRIER_TYPE_BUFFER;
                group.NumBarriers = static_cast<UINT>(d3dBufBarriers.size());
                group.pBufferBarriers = d3dBufBarriers.data();
                barrierGroups.push_back(group);
            }
            
            if (!d3dGlobBarriers.empty())
            {
                D3D12_BARRIER_GROUP group = {};
                group.Type = D3D12_BARRIER_TYPE_GLOBAL;
                group.NumBarriers = static_cast<UINT>(d3dGlobBarriers.size());
                group.pGlobalBarriers = d3dGlobBarriers.data();
                barrierGroups.push_back(group);
            }
            
            // Execute barriers if any
            if (!barrierGroups.empty() && pQueue->m_pCommandList7)
            {
                pQueue->m_pCommandList7->Barrier(static_cast<UINT>(barrierGroups.size()), barrierGroups.data());
            }
            
            sched.CompleteTask(id);
        },
        this,
        textureBarriers,
        bufferBarriers,
        globalBarriers
    );
    
    // no explicit dependency add/schedule needed (atomic API)
    
    return taskId;
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::CreateGpuSyncPoint(
    UINT64 fenceValue,
    Canvas::TaskID dependsOn)
{
    Canvas::TaskID deps1[1] = { dependsOn };
    auto taskId = m_TaskScheduler.AllocateAndScheduleTypedTask(
        (dependsOn != Canvas::InvalidTaskID) ? deps1 : nullptr,
        (dependsOn != Canvas::InvalidTaskID) ? 1u : 0u,
        [](Canvas::TaskID id, Canvas::TaskScheduler& sched, 
           CRenderQueue12* pQueue, UINT64 value)
        {
            // Signal the fence
            pQueue->m_pCommandQueue->Signal(pQueue->m_pFence, value);
            
            // Store sync point for later wait
            pQueue->m_GpuSyncPoints[id] = GpuSyncPoint{ value, pQueue->m_pFence };
            
            sched.CompleteTask(id);
        },
        this,
        fenceValue
    );
    // atomic API handles dependencies and scheduling
    
    return taskId;
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::WaitForGpuFence(
    UINT64 fenceValue,
    Canvas::TaskID dependsOn)
{
    Canvas::TaskID deps2[1] = { dependsOn };
    auto taskId = m_TaskScheduler.AllocateAndScheduleTypedTask(
        (dependsOn != Canvas::InvalidTaskID) ? deps2 : nullptr,
        (dependsOn != Canvas::InvalidTaskID) ? 1u : 0u,
        [](Canvas::TaskID id, Canvas::TaskScheduler& sched, 
           CRenderQueue12* pQueue, UINT64 value)
        {
            // Wait for GPU fence completion
            if (pQueue->m_pFence->GetCompletedValue() < value)
            {
                HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                pQueue->m_pFence->SetEventOnCompletion(value, hEvent);
                WaitForSingleObject(hEvent, INFINITE);
                CloseHandle(hEvent);
            }
            
            sched.CompleteTask(id);
        },
        this,
        fenceValue
    );
    // atomic API handles dependencies and scheduling
    
    return taskId;
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::SubmitCommandList(Canvas::TaskID dependsOn)
{
    Canvas::TaskID deps3[1] = { dependsOn };
    auto taskId = m_TaskScheduler.AllocateAndScheduleTypedTask(
        (dependsOn != Canvas::InvalidTaskID) ? deps3 : nullptr,
        (dependsOn != Canvas::InvalidTaskID) ? 1u : 0u,
        [](Canvas::TaskID id, Canvas::TaskScheduler& sched, CRenderQueue12* pQueue)
        {
            // Close and submit command list
            pQueue->m_pCommandList->Close();
            
            ID3D12CommandList* cmdLists[] = { pQueue->m_pCommandList };
            pQueue->m_pCommandQueue->ExecuteCommandLists(1, cmdLists);
            
            // Signal fence
            pQueue->m_pCommandQueue->Signal(pQueue->m_pFence, ++pQueue->m_FenceValue);
            
            sched.CompleteTask(id);
        },
        this
    );
    // After creating the submit task, schedule any pending host-write release tasks so they
    // run after this submit completes. We allocated those release tasks earlier via
    // AllocateTypedTask in ScheduleHostWriteRelease but deferred scheduling until now.
    if (taskId != Canvas::InvalidTaskID)
    {
        std::vector<std::pair<Canvas::TaskID, Canvas::TaskID>> pending;
        pending.swap(m_PendingHostWriteReleaseTasks);

        for (auto &entry : pending)
        {
            Canvas::TaskID releaseTaskId = entry.first;
            Canvas::TaskID preDep = entry.second;

            // If there's a pre-dependency (e.g., the recording/copy task), ensure the submit
            // depends on that task so the submit runs after recording finishes.
            if (preDep != Canvas::InvalidTaskID && preDep != taskId)
            {
                m_TaskScheduler.AddDependency(taskId, preDep);
            }

            // Make release depend on the submit task, then schedule it
            m_TaskScheduler.AddDependency(releaseTaskId, taskId);
            m_TaskScheduler.ScheduleTask(releaseTaskId);
        }
    }
    // atomic API handles dependencies and scheduling
    
    return taskId;
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::ScheduleHostWriteRelease(const Canvas::GfxSuballocation& suballocation, Canvas::TaskID dependsOn)
{
    // Allocate a release task but do NOT schedule it yet. We'll defer scheduling until
    // a SubmitCommandList task is created so the release can depend on that submit.
    Canvas::TaskID releaseTask = m_TaskScheduler.AllocateTypedTask(
        1,
        [](Canvas::TaskID id, Canvas::TaskScheduler& sched, CRenderQueue12* pQueue, Canvas::GfxSuballocation sub)
        {
            // Free the host-write region via the device
            if (pQueue && pQueue->m_pDevice)
            {
                pQueue->m_pDevice->FreeHostWriteRegion(const_cast<Canvas::GfxSuballocation&>(sub));
            }
            sched.CompleteTask(id);
        },
        this,
        suballocation
    );

    if (releaseTask == Canvas::InvalidTaskID)
    {
        // Allocation failed; as a fallback, create a fence-waited task immediately
        UINT64 fenceValueToWait = m_FenceValue + 1;
        Canvas::TaskID waitTask = WaitForGpuFence(fenceValueToWait, dependsOn);
        Canvas::TaskID deps[1] = { waitTask };
        auto immediateRelease = m_TaskScheduler.AllocateAndScheduleTypedTask(
            deps,
            1,
            [](Canvas::TaskID id, Canvas::TaskScheduler& sched, CRenderQueue12* pQueue, Canvas::GfxSuballocation sub)
            {
                if (pQueue && pQueue->m_pDevice)
                    pQueue->m_pDevice->FreeHostWriteRegion(const_cast<Canvas::GfxSuballocation&>(sub));
                sched.CompleteTask(id);
            },
            this,
            suballocation
        );
        (void)immediateRelease;
        return;
    }

    // Store release task and the original pre-dependency (copy/recording task) so SubmitCommandList
    // can make the submit depend on the recording before scheduling the release.
    m_PendingHostWriteReleaseTasks.emplace_back(releaseTask, dependsOn);
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::PrepareForPresent(
    Canvas::XGfxSwapChain* pSwapChain,
    const Canvas::TaskID* pDependencies,
    size_t numDependencies)
{
    CSwapChain12* pIntSwapChain = reinterpret_cast<CSwapChain12*>(pSwapChain);
    
    // Ensure swap chain back buffer is transitioned to PRESENT layout
    // EnsureTextureLayout only accepts a single dependency, so pass the first one
    Canvas::TaskID firstDep = (numDependencies > 0) ? pDependencies[0] : Canvas::InvalidTaskID;
    
    Canvas::TaskID layoutTask = EnsureTextureLayout(
        pIntSwapChain->m_pSurface->GetD3DResource(),
        D3D12_BARRIER_SYNC_NONE,
        D3D12_BARRIER_ACCESS_NO_ACCESS,
        D3D12_BARRIER_LAYOUT_PRESENT,
        0xFFFFFFFF,
        D3D12_TEXTURE_BARRIER_FLAG_NONE,
        firstDep
    );
    
    // Add remaining leaf tasks as additional dependencies
    for (size_t i = 1; i < numDependencies; ++i)
    {
        m_TaskScheduler.AddDependency(layoutTask, pDependencies[i]);
    }
    
    return layoutTask;
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::SchedulePresent(
    Canvas::XGfxSwapChain* pSwapChain,
    Canvas::TaskID dependsOn)
{
    CSwapChain12* pIntSwapChain = reinterpret_cast<CSwapChain12*>(pSwapChain);
    
    Canvas::TaskID deps[1] = { dependsOn };
    auto taskId = m_TaskScheduler.AllocateAndScheduleTypedTask(
        (dependsOn != Canvas::InvalidTaskID) ? deps : nullptr,
        (dependsOn != Canvas::InvalidTaskID) ? 1u : 0u,
        [](Canvas::TaskID id, Canvas::TaskScheduler& sched, CSwapChain12* pSwapChain)
        {
            // Execute present operation
            Gem::ThrowGemError(pSwapChain->Present());
            
            // Complete task
            sched.CompleteTask(id);
        },
        pIntSwapChain
    );
    
    return taskId;
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::ProcessCompletedWork()
{
    // Retire completed tasks to reclaim memory
    m_TaskScheduler.RetireCompletedTasks();
    
    // Clean up old sync points
    UINT64 completedValue = m_pFence->GetCompletedValue();
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

//------------------------------------------------------------------------------------------------
void CRenderQueue12::FlushPendingBarriers()
{
    // This function must be called with m_mutex locked
    
    if (m_PendingTextureBarriers.empty() && 
        m_PendingBufferBarriers.empty() && 
        m_PendingGlobalBarriers.empty())
    {
        return;  // Nothing to flush
    }
    
    // Convert to D3D12 barrier structures
    std::vector<D3D12_TEXTURE_BARRIER> d3dTexBarriers;
    std::vector<D3D12_BUFFER_BARRIER> d3dBufBarriers;
    std::vector<D3D12_GLOBAL_BARRIER> d3dGlobBarriers;
    
    d3dTexBarriers.reserve(m_PendingTextureBarriers.size());
    d3dBufBarriers.reserve(m_PendingBufferBarriers.size());
    d3dGlobBarriers.reserve(m_PendingGlobalBarriers.size());
    
    // Convert texture barriers
    for (const auto& barrier : m_PendingTextureBarriers)
    {
        D3D12_TEXTURE_BARRIER d3dBarrier = {};
        d3dBarrier.SyncBefore = barrier.SyncBefore;
        d3dBarrier.SyncAfter = barrier.SyncAfter;
        d3dBarrier.AccessBefore = barrier.AccessBefore;
        d3dBarrier.AccessAfter = barrier.AccessAfter;
        d3dBarrier.LayoutBefore = barrier.LayoutBefore;
        d3dBarrier.LayoutAfter = barrier.LayoutAfter;
        d3dBarrier.pResource = barrier.pResource;
        d3dBarrier.Subresources.IndexOrFirstMipLevel = barrier.Subresources;
        d3dBarrier.Flags = barrier.Flags;
        d3dTexBarriers.push_back(d3dBarrier);
    }
    
    // Convert buffer barriers
    for (const auto& barrier : m_PendingBufferBarriers)
    {
        D3D12_BUFFER_BARRIER d3dBarrier = {};
        d3dBarrier.SyncBefore = barrier.SyncBefore;
        d3dBarrier.SyncAfter = barrier.SyncAfter;
        d3dBarrier.AccessBefore = barrier.AccessBefore;
        d3dBarrier.AccessAfter = barrier.AccessAfter;
        d3dBarrier.pResource = barrier.pResource;
        d3dBarrier.Offset = barrier.Offset;
        d3dBarrier.Size = barrier.Size;
        d3dBufBarriers.push_back(d3dBarrier);
    }
    
    // Convert global barriers
    for (const auto& barrier : m_PendingGlobalBarriers)
    {
        D3D12_GLOBAL_BARRIER d3dBarrier = {};
        d3dBarrier.SyncBefore = barrier.SyncBefore;
        d3dBarrier.SyncAfter = barrier.SyncAfter;
        d3dBarrier.AccessBefore = barrier.AccessBefore;
        d3dBarrier.AccessAfter = barrier.AccessAfter;
        d3dGlobBarriers.push_back(d3dBarrier);
    }
    
    // Build barrier groups
    std::vector<D3D12_BARRIER_GROUP> barrierGroups;
    barrierGroups.reserve(3);
    
    if (!d3dTexBarriers.empty())
    {
        D3D12_BARRIER_GROUP group = {};
        group.Type = D3D12_BARRIER_TYPE_TEXTURE;
        group.NumBarriers = static_cast<UINT>(d3dTexBarriers.size());
        group.pTextureBarriers = d3dTexBarriers.data();
        barrierGroups.push_back(group);
    }
    
    if (!d3dBufBarriers.empty())
    {
        D3D12_BARRIER_GROUP group = {};
        group.Type = D3D12_BARRIER_TYPE_BUFFER;
        group.NumBarriers = static_cast<UINT>(d3dBufBarriers.size());
        group.pBufferBarriers = d3dBufBarriers.data();
        barrierGroups.push_back(group);
    }
    
    if (!d3dGlobBarriers.empty())
    {
        D3D12_BARRIER_GROUP group = {};
        group.Type = D3D12_BARRIER_TYPE_GLOBAL;
        group.NumBarriers = static_cast<UINT>(d3dGlobBarriers.size());
        group.pGlobalBarriers = d3dGlobBarriers.data();
        barrierGroups.push_back(group);
    }
    
    // Execute barriers
    if (!barrierGroups.empty() && m_pCommandList7)
    {
        m_pCommandList7->Barrier(static_cast<UINT>(barrierGroups.size()), barrierGroups.data());
    }
    
    // Clear pending barriers
    m_PendingTextureBarriers.clear();
    m_PendingBufferBarriers.clear();
    m_PendingGlobalBarriers.clear();
}
