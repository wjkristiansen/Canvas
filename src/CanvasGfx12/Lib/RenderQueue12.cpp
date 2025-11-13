//================================================================================================
// RenderQueue12 - D3D12 Command Queue and Rendering State Management
//
// THREADING MODEL:
// - NOT THREAD-SAFE: All methods must be called from a single thread
// - Concurrent access from multiple threads will cause undefined behavior
// - If multi-threaded rendering is required, use multiple RenderQueue instances
// - TaskManager handles task dependency ordering with immediate inline execution
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
    m_TaskManager(1024 * 1024)  // 1MB initial ring buffer for task allocation
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

    // Create dedicated fixup command list with its own allocator (reused across submissions)
    // NOTE: Cannot share allocator with main command list - D3D12 requires separate allocators
    // when multiple command lists may be recording simultaneously
    CComPtr<ID3D12CommandAllocator> pFixupCA;
    Gem::ThrowGemError(ResultFromHRESULT(pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pFixupCA))));
    
    CComPtr<ID3D12GraphicsCommandList> pFixupCL;
    Gem::ThrowGemError(ResultFromHRESULT(pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pFixupCA, nullptr, IID_PPV_ARGS(&pFixupCL))));
    
    // QueryInterface to ID3D12GraphicsCommandList7 (required for Barrier() API)
    CComPtr<ID3D12GraphicsCommandList7> pFixupCL7;
    Gem::ThrowGemError(ResultFromHRESULT(pFixupCL->QueryInterface(IID_PPV_ARGS(&pFixupCL7))));
    
    // Close the fixup command list immediately (will be reset when needed)
    pFixupCL7->Close();

    m_pShaderResourceDescriptorHeap.Attach(pResDH.Detach());
    m_pSamplerDescriptorHeap.Attach(pSamplerDH.Detach());
    m_pRTVDescriptorHeap.Attach(pRTVDH.Detach());
    m_pDSVDescriptorHeap.Attach(pDSVDH.Detach());
    m_pCommandAllocator.Attach(pCA.Detach());
    m_pCommandList.Attach(pCL.Detach());
    m_pCommandQueue.Attach(pCQ.Detach());
    m_pFence.Attach(pFence.Detach());
    m_pFixupCommandAllocator.Attach(pFixupCA.Detach());
    m_pFixupCommandList.Attach(pFixupCL7.Detach());

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
    
    // Declare resource usage - source as read, destination as write
    TaskResourceUsageBuilder usages;
    usages.BufferAsCopySource(pSourceBuffer->GetD3DResource());
    usages.BufferAsCopyDest(pDestBuffer->GetD3DResource());
    
    // Schedule copy operation with automatic barrier insertion
    RecordCommands(
        usages.Build(),
        [pDestBuffer, pSourceBuffer](ID3D12GraphicsCommandList* cmdList) {
            cmdList->CopyResource(pDestBuffer->GetD3DResource(), pSourceBuffer->GetD3DResource());
        },
        nullptr,
        0,
        "CopyBuffer");
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CRenderQueue12::ClearSurface(Canvas::XGfxSurface *pGfxSurface, const float Color[4])
{
    CSurface12 *pSurface = reinterpret_cast<CSurface12 *>(pGfxSurface);
    // Declare render target usage - automatically handles layout transition and barrier insertion
    TaskResourceUsageBuilder usages;
    usages.TextureAsRenderTarget(pSurface->GetD3DResource());
    // Generate barriers immediately
    GenerateBarriersForRecording(usages.Build());
    FlushPendingBarriers();
    // Record directly to command list
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = CreateRenderTargetView(pSurface, 0, 0, 0);
    m_pCommandList->ClearRenderTargetView(rtv, Color, 0, nullptr);
    // Update recording state for next operation
    UpdateRecordingState(usages.Build());
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
GEMMETHODIMP CRenderQueue12::FlushAndPresent(Canvas::XGfxSwapChain *pSwapChain)
{
    try
    {
        CSwapChain12 *pIntSwapChain = reinterpret_cast<CSwapChain12 *>(pSwapChain);
        pIntSwapChain->WaitForFrameLatency();

        // Inline present transition if needed
        if (pIntSwapChain->m_BackBufferModified)
        {
            // Insert transition barrier to PRESENT
            TaskResourceUsageBuilder usages;
            usages.TextureAsRenderTarget(pIntSwapChain->m_pSurface->GetD3DResource());
            GenerateBarriersForRecording(usages.Build());
            FlushPendingBarriers();
            pIntSwapChain->m_BackBufferModified = false;
        }

        // Submit command list to GPU using proper SubmitCommandList method
        Canvas::TaskID submitTask = SubmitCommandList(Canvas::NullTaskID, TextureLayoutBuilder());

        // Present (depends on submit)
        Canvas::TaskID presentTask = EnqueueTask(
            &submitTask, 1, "Present",
            [](Canvas::TaskID id, Canvas::TaskManager& sched, CSwapChain12* pSwap) {
                pSwap->Present();
                sched.CompleteTask(id);
            },
            pIntSwapChain);

        // Reset command list (must wait for submit to complete)
        EnqueueTask(
            &submitTask, 1, "CommandListReset",
            [](Canvas::TaskID id, Canvas::TaskManager& sched, CRenderQueue12* pQueue) {
                pQueue->m_pCommandAllocator = pQueue->m_CommandAllocatorPool.RotateAllocators(pQueue);
                pQueue->m_pCommandAllocator->Reset();
                ThrowFailedHResult(pQueue->m_pCommandList->Reset(pQueue->m_pCommandAllocator, nullptr));
                pQueue->m_pFixupCommandAllocator->Reset();  // Reset fixup allocator after GPU completes
                pQueue->m_PendingTextureBarriers.clear();
                pQueue->m_PendingBufferBarriers.clear();
                pQueue->m_PendingGlobalBarriers.clear();
                pQueue->m_RecordingResourceState.clear();
                sched.CompleteTask(id);
            },
            this);

        // Store present task so next frame can depend on it
        pIntSwapChain->m_LastFramePresentTask = presentTask;

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
// Task-based GPU workload management implementation
//------------------------------------------------------------------------------------------------


//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::CreateGpuSyncPoint(
    UINT64 fenceValue,
    Canvas::TaskID dependsOn)
{
    Canvas::TaskID deps[1] = { dependsOn };
    return EnqueueTask(
        (dependsOn != Canvas::NullTaskID) ? deps : nullptr,
        (dependsOn != Canvas::NullTaskID) ? 1u : 0u,
        "GpuSyncPoint",
        [](Canvas::TaskID id, Canvas::TaskManager& sched, 
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
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::WaitForGpuFence(
    UINT64 fenceValue,
    Canvas::TaskID dependsOn)
{
    Canvas::TaskID deps[1] = { dependsOn };
    return EnqueueTask(
        (dependsOn != Canvas::NullTaskID) ? deps : nullptr,
        (dependsOn != Canvas::NullTaskID) ? 1u : 0u,
        "WaitForGpuFence",
        [](Canvas::TaskID id, Canvas::TaskManager& sched, 
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
}

//------------------------------------------------------------------------------------------------
// Call this after creating a texture resource to set its initial layout
void CRenderQueue12::UpdateTextureLayouts(const TextureLayoutBuilder& builder)
{
    auto inits = builder.Build();
    for (const auto& init : inits)
    {
        m_TextureCurrentLayouts[init.pResource].SetLayout(init.subresource, init.layout);
    }
}

//------------------------------------------------------------------------------------------------
// Per-subresource layout fixup generation
//
// KEY BEHAVIORS:
// 1. Single subresource transition (exp.subresource != 0xFFFFFFFF):
//    - Looks up current layout for that specific subresource
//    - Generates one barrier for that subresource
//    - Splits from uniform to per-subresource tracking if needed
//
// 2. All subresources transition (exp.subresource == 0xFFFFFFFF):
//    - If currently uniform: generates single barrier for all subresources
//    - If mixed layouts: generates individual barriers for each tracked subresource
//    - Collapses back to uniform layout after transition
//
// 3. Invalid transition prevention:
//    - If requesting "all subresources" transition but they have different layouts,
//      we generate barriers from each subresource's ACTUAL current layout, not assumed
//    - This prevents D3D12 validation errors from incorrect barrier states
//
void CRenderQueue12::AddLayoutFixups(const std::vector<TextureLayout>& expectedLayouts)
{
    // Build complete D3D12 barriers and accumulate them - actual command list created at submission
    for (const auto& exp : expectedLayouts)
    {
        if (!exp.pResource) continue;
        
        auto& state = m_TextureCurrentLayouts[exp.pResource];
        
        if (exp.subresource == 0xFFFFFFFF)
        {
            // Transitioning ALL subresources
            
            // Check if we can do this as a single barrier (all subresources currently have same layout)
            D3D12_BARRIER_LAYOUT uniformCurrent;
            if (state.CanCollapseToUniform(uniformCurrent))
            {
                // All subresources have the same layout - single barrier
                if (uniformCurrent == exp.layout)
                {
                    // Already in desired layout; skip
                    continue;
                }
                
                D3D12_TEXTURE_BARRIER b = {};
                b.pResource = exp.pResource;
                b.LayoutBefore = uniformCurrent;
                b.LayoutAfter = exp.layout;
                b.SyncBefore = D3D12_BARRIER_SYNC_NONE;
                b.SyncAfter = D3D12_BARRIER_SYNC_ALL;
                b.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
                b.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
                b.Subresources.IndexOrFirstMipLevel = 0xFFFFFFFF;  // All subresources
                b.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
                
                m_PendingLayoutFixupBarriers.push_back(b);
            }
            else
            {
                // Subresources have different layouts - need individual barriers for each
                // This is the tricky case: we need to know total subresource count
                // For now, transition only the ones we're tracking (on-demand approach)
                if (!state.perSubresourceLayouts.empty())
                {
                    for (const auto& [subresource, currentLayout] : state.perSubresourceLayouts)
                    {
                        if (currentLayout == exp.layout) continue;  // Skip if already in target layout
                        
                        D3D12_TEXTURE_BARRIER b = {};
                        b.pResource = exp.pResource;
                        b.LayoutBefore = currentLayout;
                        b.LayoutAfter = exp.layout;
                        b.SyncBefore = D3D12_BARRIER_SYNC_NONE;
                        b.SyncAfter = D3D12_BARRIER_SYNC_ALL;
                        b.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
                        b.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
                        b.Subresources.IndexOrFirstMipLevel = subresource;
                        b.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
                        
                        m_PendingLayoutFixupBarriers.push_back(b);
                    }
                }
            }
            
            // Update: all subresources now uniform
            state.SetLayout(0xFFFFFFFF, exp.layout);
        }
        else
        {
            // Transitioning a SINGLE subresource
            D3D12_BARRIER_LAYOUT currentLayout = state.GetLayout(exp.subresource);
            
            if (currentLayout == exp.layout)
            {
                // Already in desired layout; skip
                continue;
            }
            
            // Build single-subresource barrier
            D3D12_TEXTURE_BARRIER b = {};
            b.pResource = exp.pResource;
            b.LayoutBefore = currentLayout;
            b.LayoutAfter = exp.layout;
            b.SyncBefore = D3D12_BARRIER_SYNC_NONE;
            b.SyncAfter = D3D12_BARRIER_SYNC_ALL;
            b.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
            b.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
            b.Subresources.IndexOrFirstMipLevel = exp.subresource;
            b.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
            
            m_PendingLayoutFixupBarriers.push_back(b);
            
            // Update: this specific subresource changed
            state.SetLayout(exp.subresource, exp.layout);
        }
    }
}

//------------------------------------------------------------------------------------------------
// Builder-style overloads for convenience
//------------------------------------------------------------------------------------------------
void CRenderQueue12::AddLayoutFixups(const TextureLayoutBuilder& builder)
{
    AddLayoutFixups(builder.Build());
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::SubmitCommandList(
    Canvas::TaskID dependsOn,
    const TextureLayoutBuilder& expectedLayouts)
{
    Canvas::TaskID deps[1] = { dependsOn };
    auto taskId = EnqueueTask(
        (dependsOn != Canvas::NullTaskID) ? deps : nullptr,
        (dependsOn != Canvas::NullTaskID) ? 1u : 0u,
        "SubmitCommandList",
        [expectedLayouts](Canvas::TaskID id, Canvas::TaskManager& sched, CRenderQueue12* pQueue)
        {
            // If expectedLayouts provided, queue fixups (batch style)
            auto layouts = expectedLayouts.Build();
            if (!layouts.empty())
            {
                pQueue->AddLayoutFixups(layouts);
            }

            // Close main command list
            pQueue->m_pCommandList->Close();

            // Record fixup barriers if needed
            bool hasFixups = !pQueue->m_PendingLayoutFixupBarriers.empty();
            
            if (hasFixups)
            {
                // Reset and reuse the dedicated fixup command list with its own allocator
                ThrowFailedHResult(pQueue->m_pFixupCommandList->Reset(pQueue->m_pFixupCommandAllocator, nullptr));
                
                // Issue all barriers as a single group
                D3D12_BARRIER_GROUP group = {};
                group.Type = D3D12_BARRIER_TYPE_TEXTURE;
                group.NumBarriers = static_cast<UINT>(pQueue->m_PendingLayoutFixupBarriers.size());
                group.pTextureBarriers = pQueue->m_PendingLayoutFixupBarriers.data();
                
                pQueue->m_pFixupCommandList->Barrier(1, &group);
                
                ThrowFailedHResult(pQueue->m_pFixupCommandList->Close());
                
                // Clear pending barriers
                pQueue->m_PendingLayoutFixupBarriers.clear();
            }

            // Build submission array: fixup list (if exists) then main list
            std::vector<ID3D12CommandList*> submitLists;
            if (hasFixups)
            {
                submitLists.push_back(pQueue->m_pFixupCommandList);
            }
            submitLists.push_back(pQueue->m_pCommandList);

            pQueue->m_pCommandQueue->ExecuteCommandLists(static_cast<UINT>(submitLists.size()), submitLists.data());

            // Signal fence
            pQueue->m_pCommandQueue->Signal(pQueue->m_pFence, ++pQueue->m_FenceValue);
            sched.CompleteTask(id);
        },
        this
    );
    
    // After creating the submit task, schedule any pending host-write release tasks so they
    // run after this submit completes. We allocated those release tasks earlier via
    // AllocateTypedTask in ScheduleHostWriteRelease but deferred scheduling until now.
    if (taskId != Canvas::NullTaskID)
    {
        std::vector<std::pair<Canvas::TaskID, Canvas::TaskID>> pending;
        pending.swap(m_PendingHostWriteReleaseTasks);

        for (auto &entry : pending)
        {
            Canvas::TaskID releaseTaskId = entry.first;
            Canvas::TaskID preDep = entry.second;

            // If there's a pre-dependency (e.g., the recording/copy task), ensure the submit
            // depends on that task so the submit runs after recording finishes.
            if (preDep != Canvas::NullTaskID && preDep != taskId)
            {
                AddDependency(taskId, preDep);
            }

            // Make release depend on the submit task, then schedule it
            AddDependency(releaseTaskId, taskId);
            EnqueueTask(releaseTaskId, "HostWriteRelease");
        }
    }
    
    return taskId;
}
void CRenderQueue12::ScheduleHostWriteRelease(const Canvas::GfxSuballocation& suballocation, Canvas::TaskID dependsOn)
{
    // Allocate a release task but do NOT schedule it yet. We'll defer scheduling until
    // a SubmitCommandList task is created so the release can depend on that submit.
    Canvas::TaskID releaseTask = AllocateTypedTask(
        1,
        "HostWriteRelease",
        [](Canvas::TaskID id, Canvas::TaskManager& sched, CRenderQueue12* pQueue, Canvas::GfxSuballocation sub)
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

    if (releaseTask == Canvas::NullTaskID)
    {
        // Allocation failed; as a fallback, create a fence-waited task immediately
        UINT64 fenceValueToWait = m_FenceValue + 1;
        Canvas::TaskID waitTask = WaitForGpuFence(fenceValueToWait, dependsOn);
        Canvas::TaskID deps[1] = { waitTask };
        auto immediateRelease = EnqueueTask(
            deps,
            1,
            "HostWriteReleaseFallback",
            [](Canvas::TaskID id, Canvas::TaskManager& sched, CRenderQueue12* pQueue, Canvas::GfxSuballocation sub)
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
Canvas::TaskID CRenderQueue12::RecordCommands(
    const TaskResourceUsages& resourceUsages,
    std::function<void(ID3D12GraphicsCommandList*)> recordFunc,
    const Canvas::TaskID* pDependencies,
    size_t numDependencies,
    PCSTR taskName)
{
    // Validate resource usage patterns (when diagnostics enabled)
    if (!ValidateResourceUsageNoWriteConflicts(resourceUsages))
    {
        throw Gem::GemError(Gem::Result::InvalidArg);
    }
    
    // Generate barriers based on current recording state (linear, CPU timeline)
    GenerateBarriersForRecording(resourceUsages);
    
    // Use provided task name or default if not specified
    const char* effectiveTaskName = taskName ? taskName : "RecordCommands";
    
    // Build combined dependency list: implicit last command list task + user dependencies
    std::vector<Canvas::TaskID> allDeps;
    
    // Add implicit dependency on last command list task (maintains linear command order)
    if (m_LastCommandListTask != Canvas::NullTaskID)
    {
        allDeps.push_back(m_LastCommandListTask);
    }
    
    // Add all user-provided dependencies (avoid duplicates)
    for (size_t i = 0; i < numDependencies; ++i)
    {
        if (pDependencies[i] != Canvas::NullTaskID)
        {
            // Skip if it's the same as the implicit dependency
            if (pDependencies[i] != m_LastCommandListTask)
            {
                allDeps.push_back(pDependencies[i]);
            }
        }
    }
    
    // Create and schedule task that will flush barriers and execute commands
    Canvas::TaskID taskId = EnqueueTask(
        allDeps.empty() ? nullptr : allDeps.data(),
        static_cast<uint32_t>(allDeps.size()),
        effectiveTaskName,
        [this, recordFunc](Canvas::TaskID taskId, Canvas::TaskManager& sched)
        {
            // Flush pending barriers into command list
            FlushPendingBarriers();
            
            // Execute the command recording function
            recordFunc(m_pCommandList);
            
            sched.CompleteTask(taskId);
        });
    
    // Update recording state (linear forward propagation)
    UpdateRecordingState(resourceUsages);
    
    // Save output layouts to submission state so future command executions know the layout
    // This bridges recording state (Tier 1) to submission state (Tier 2)
    for (const auto& texUsage : resourceUsages.TextureUsages)
    {
        if (texUsage.IsValid())
        {
            m_SubmissionOutputLayouts[taskId].TextureLayouts[texUsage.pResource] = texUsage.RequiredLayout;
        }
    }
    
    // Update last command list task to this task
    m_LastCommandListTask = taskId;
    
    return taskId;
}

//------------------------------------------------------------------------------------------------
Canvas::TaskID CRenderQueue12::SchedulePresent(
    Canvas::XGfxSwapChain* pSwapChain,
    Canvas::TaskID dependsOn)
{
    CSwapChain12* pIntSwapChain = reinterpret_cast<CSwapChain12*>(pSwapChain);
    
    Canvas::TaskID deps[1] = { dependsOn };
    return EnqueueTask(
        (dependsOn != Canvas::NullTaskID) ? deps : nullptr,
        (dependsOn != Canvas::NullTaskID) ? 1u : 0u,
        "SwapChainPresent",
        [](Canvas::TaskID id, Canvas::TaskManager& sched, CSwapChain12* pSwapChain)
        {
            // Execute present operation
            Gem::ThrowGemError(pSwapChain->Present());
            
            // Complete task
            sched.CompleteTask(id);
        },
        pIntSwapChain
    );
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::ProcessCompletedWork()
{
    // Retire completed tasks to reclaim memory
    m_TaskManager.RetireCompletedTasks();
    
    // Clean up old sync points - check all entries for GPU completion
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
    
    // Clean up submission output layouts - check all task states
    // Remove tasks that are Completed or Invalid (not Active/Enqueued)
    auto stats = m_TaskManager.GetStatistics();
    
    for (auto it = m_SubmissionOutputLayouts.begin(); it != m_SubmissionOutputLayouts.end();)
    {
        Canvas::TaskState state = m_TaskManager.GetTaskState(it->first);
        
        // Remove if task is Completed/Invalid, or if TaskID is ancient
        // Keep only Active/Enqueued tasks (waiting for execution or dependencies)
        if (state == Canvas::TaskState::Completed || 
            state == Canvas::TaskState::Invalid ||
            (stats.NextTaskId - it->first) > 10000)
        {
            it = m_SubmissionOutputLayouts.erase(it);
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

//------------------------------------------------------------------------------------------------
// Resource-Aware Task Scheduling Implementation
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
bool CRenderQueue12::ValidateResourceUsageNoWriteConflicts(const TaskResourceUsages& resourceUsages) const
{
    // Validate that the task itself doesn't declare conflicting writes
    return resourceUsages.IsValidNoWriteConflicts();
}

//------------------------------------------------------------------------------------------------
CRenderQueue12::ResourceStateSnapshot CRenderQueue12::GetResourceState(ID3D12Resource* pResource) const
{
    ResourceStateSnapshot snapshot;
    
    // Query recording state (Tier 1)
    auto it = m_RecordingResourceState.find(pResource);
    if (it != m_RecordingResourceState.end())
    {
        const auto& layoutState = it->second.TextureLayouts;
        
        // Copy layout information to snapshot
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
// TIER 1: Command Buffer Recording - Generate barriers based on linear recording state
// Now with per-subresource layout tracking (same logic as AddLayoutFixups)
//------------------------------------------------------------------------------------------------
void CRenderQueue12::GenerateBarriersForRecording(const TaskResourceUsages& resourceUsages)
{
    const UINT MaxBarriersToGenerate = 256;
    UINT barriersGenerated = 0;
    
    // For textures, generate transitions based on current recording state
    for (const auto& texUsage : resourceUsages.TextureUsages)
    {
        if (!texUsage.IsValid() || barriersGenerated >= MaxBarriersToGenerate)
            continue;
        
        // Get current recording state for this resource
        D3D12_BARRIER_SYNC currentSync = D3D12_BARRIER_SYNC_NONE;
        D3D12_BARRIER_ACCESS currentAccess = D3D12_BARRIER_ACCESS_NO_ACCESS;
        
        auto it = m_RecordingResourceState.find(texUsage.pResource);
        if (it != m_RecordingResourceState.end())
        {
            currentSync = it->second.Sync;
            currentAccess = it->second.Access;
        }
        
        // Handle per-subresource layout transitions
        if (texUsage.Subresources == 0xFFFFFFFF)
        {
            // Transitioning ALL subresources
            D3D12_BARRIER_LAYOUT currentLayout = D3D12_BARRIER_LAYOUT_COMMON;
            
            if (it != m_RecordingResourceState.end())
            {
                auto& layoutState = it->second.TextureLayouts;
                
                // Check if we can do single barrier (all subresources have same layout)
                D3D12_BARRIER_LAYOUT uniformCurrent;
                if (layoutState.CanCollapseToUniform(uniformCurrent))
                {
                    currentLayout = uniformCurrent;
                    
                    // Generate single barrier if layout change needed
                    if (currentLayout != texUsage.RequiredLayout)
                    {
                        TextureBarrier barrier;
                        barrier.pResource = texUsage.pResource;
                        barrier.LayoutBefore = currentLayout;
                        barrier.LayoutAfter = texUsage.RequiredLayout;
                        barrier.SyncBefore = currentSync;
                        barrier.SyncAfter = texUsage.SyncForUsage;
                        barrier.AccessBefore = currentAccess;
                        barrier.AccessAfter = texUsage.AccessForUsage;
                        barrier.Subresources = 0xFFFFFFFF;
                        barrier.Flags = texUsage.Flags;
                        
                        m_PendingTextureBarriers.push_back(barrier);
                        barriersGenerated++;
                    }
                }
                else
                {
                    // Mixed layouts - need individual barriers per tracked subresource
                    if (!layoutState.perSubresourceLayouts.empty())
                    {
                        for (const auto& [subresource, subLayout] : layoutState.perSubresourceLayouts)
                        {
                            if (subLayout == texUsage.RequiredLayout)
                                continue; // Skip if already in target layout
                            
                            if (barriersGenerated >= MaxBarriersToGenerate)
                                break;
                            
                            TextureBarrier barrier;
                            barrier.pResource = texUsage.pResource;
                            barrier.LayoutBefore = subLayout;
                            barrier.LayoutAfter = texUsage.RequiredLayout;
                            barrier.SyncBefore = currentSync;
                            barrier.SyncAfter = texUsage.SyncForUsage;
                            barrier.AccessBefore = currentAccess;
                            barrier.AccessAfter = texUsage.AccessForUsage;
                            barrier.Subresources = subresource;
                            barrier.Flags = texUsage.Flags;
                            
                            m_PendingTextureBarriers.push_back(barrier);
                            barriersGenerated++;
                        }
                    }
                    else
                    {
                        // No tracked subresources but state says mixed - assume COMMON and do all
                        TextureBarrier barrier;
                        barrier.pResource = texUsage.pResource;
                        barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
                        barrier.LayoutAfter = texUsage.RequiredLayout;
                        barrier.SyncBefore = currentSync;
                        barrier.SyncAfter = texUsage.SyncForUsage;
                        barrier.AccessBefore = currentAccess;
                        barrier.AccessAfter = texUsage.AccessForUsage;
                        barrier.Subresources = 0xFFFFFFFF;
                        barrier.Flags = texUsage.Flags;
                        
                        m_PendingTextureBarriers.push_back(barrier);
                        barriersGenerated++;
                    }
                }
            }
            else
            {
                // No tracking yet - assume COMMON for all subresources
                if (D3D12_BARRIER_LAYOUT_COMMON != texUsage.RequiredLayout)
                {
                    TextureBarrier barrier;
                    barrier.pResource = texUsage.pResource;
                    barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
                    barrier.LayoutAfter = texUsage.RequiredLayout;
                    barrier.SyncBefore = currentSync;
                    barrier.SyncAfter = texUsage.SyncForUsage;
                    barrier.AccessBefore = currentAccess;
                    barrier.AccessAfter = texUsage.AccessForUsage;
                    barrier.Subresources = 0xFFFFFFFF;
                    barrier.Flags = texUsage.Flags;
                    
                    m_PendingTextureBarriers.push_back(barrier);
                    barriersGenerated++;
                }
            }
        }
        else
        {
            // Transitioning SINGLE subresource
            D3D12_BARRIER_LAYOUT currentLayout = D3D12_BARRIER_LAYOUT_COMMON;
            
            if (it != m_RecordingResourceState.end())
            {
                currentLayout = it->second.TextureLayouts.GetLayout(texUsage.Subresources);
            }
            
            // Generate barrier if layout change needed
            if (currentLayout != texUsage.RequiredLayout)
            {
                TextureBarrier barrier;
                barrier.pResource = texUsage.pResource;
                barrier.LayoutBefore = currentLayout;
                barrier.LayoutAfter = texUsage.RequiredLayout;
                barrier.SyncBefore = currentSync;
                barrier.SyncAfter = texUsage.SyncForUsage;
                barrier.AccessBefore = currentAccess;
                barrier.AccessAfter = texUsage.AccessForUsage;
                barrier.Subresources = texUsage.Subresources;
                barrier.Flags = texUsage.Flags;
                
                m_PendingTextureBarriers.push_back(barrier);
                barriersGenerated++;
            }
        }
    }
    
    // For buffers, generate access transitions based on current recording state
    for (const auto& bufUsage : resourceUsages.BufferUsages)
    {
        if (!bufUsage.IsValid() || barriersGenerated >= MaxBarriersToGenerate)
            continue;
        
        // Look up current recording state
        D3D12_BARRIER_SYNC currentSync = D3D12_BARRIER_SYNC_NONE;
        D3D12_BARRIER_ACCESS currentAccess = D3D12_BARRIER_ACCESS_NO_ACCESS;
        
        auto it = m_RecordingResourceState.find(bufUsage.pResource);
        if (it != m_RecordingResourceState.end())
        {
            currentSync = it->second.Sync;
            currentAccess = it->second.Access;
        }
        
        // Generate barrier from current state to required state
        BufferBarrier barrier;
        barrier.pResource = bufUsage.pResource;
        barrier.SyncBefore = currentSync;
        barrier.SyncAfter = bufUsage.SyncForUsage;
        barrier.AccessBefore = currentAccess;
        barrier.AccessAfter = bufUsage.AccessForUsage;
        barrier.Offset = bufUsage.Offset;
        barrier.Size = bufUsage.Size;
        
        m_PendingBufferBarriers.push_back(barrier);
        barriersGenerated++;
    }
}

//------------------------------------------------------------------------------------------------
// TIER 1: Update recording state after declaring resource usage
// Now with per-subresource layout tracking
//------------------------------------------------------------------------------------------------
void CRenderQueue12::UpdateRecordingState(const TaskResourceUsages& resourceUsages)
{
    // Update texture states - linear forward propagation with per-subresource layouts
    for (const auto& texUsage : resourceUsages.TextureUsages)
    {
        if (!texUsage.IsValid())
            continue;
        
        auto& state = m_RecordingResourceState[texUsage.pResource];
        
        // Update per-subresource layout tracking
        state.TextureLayouts.SetLayout(texUsage.Subresources, texUsage.RequiredLayout);
        
        // Sync and Access are typically uniform across subresources
        state.Sync = texUsage.SyncForUsage;
        state.Access = texUsage.AccessForUsage;
    }
    
    // Update buffer states - linear forward propagation
    for (const auto& bufUsage : resourceUsages.BufferUsages)
    {
        if (!bufUsage.IsValid())
            continue;
        
        auto& state = m_RecordingResourceState[bufUsage.pResource];
        // Buffers don't have layout
        state.Sync = bufUsage.SyncForUsage;
        state.Access = bufUsage.AccessForUsage;
    }
}

//------------------------------------------------------------------------------------------------
// TIER 2: Merge submission output layouts from dependencies (cached for efficiency)
//------------------------------------------------------------------------------------------------
CRenderQueue12::SubmissionOutputState CRenderQueue12::MergeSubmissionInputLayouts(
    const Canvas::TaskID* pDependencies,
    size_t numDependencies)
{
    SubmissionOutputState merged;
    
    // Merge output layouts from all direct dependencies
    // Later dependencies override earlier ones (last writer wins)
    for (size_t i = 0; i < numDependencies; ++i)
    {
        if (pDependencies[i] == Canvas::NullTaskID)
            continue;
        
        auto it = m_SubmissionOutputLayouts.find(pDependencies[i]);
        if (it != m_SubmissionOutputLayouts.end())
        {
            // Merge this dependency's output layouts into combined state
            for (const auto& [pResource, layout] : it->second.TextureLayouts)
            {
                merged.TextureLayouts[pResource] = layout;
            }
        }
    }
    
    return merged;
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::LogTaskEnqueued(Canvas::TaskID taskId, PCSTR taskName, const Canvas::TaskID* pDependencies, uint32_t dependencyCount, bool isImmediate)
{
    Canvas::XLogger* pLogger = m_pDevice->GetLogger();
    if (!pLogger)
        return;
    
    // Format dependency list
    std::string depList;
    if (dependencyCount == 0)
    {
        depList = "(none)";
    }
    else
    {
        depList = "[";
        for (uint32_t i = 0; i < dependencyCount; ++i)
        {
            if (i > 0)
                depList += ", ";
            if (pDependencies && pDependencies[i] != Canvas::NullTaskID)
            {
                depList += std::to_string(pDependencies[i]);
            }
            else
            {
                depList += "null";
            }
        }
        depList += "]";
    }
    
    // Log the task enqueuing with optional task name
    if (taskName)
    {
        Canvas::LogDebug(pLogger, "TaskManager: Enqueued TaskID=%llu, Name=%s, Dependencies=%s, Execution=%s", 
                         taskId, taskName, depList.c_str(), isImmediate ? "immediate" : "deferred");
    }
    else
    {
        Canvas::LogDebug(pLogger, "TaskManager: Enqueued TaskID=%llu, Dependencies=%s, Execution=%s", 
                         taskId, depList.c_str(), isImmediate ? "immediate" : "deferred");
    }
}
