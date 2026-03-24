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
#include "MeshData12.h"

#include <filesystem>
#include <fstream>

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
    DefaultRootParams[3].InitAsDescriptorTable(static_cast<UINT>(DefaultDescriptorRanges.size()), DefaultDescriptorRanges.data(), D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC DefaultRootSigDesc(4U, DefaultRootParams.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    CComPtr<ID3DBlob> pRSBlob;
    ThrowFailedHResult(D3D12SerializeVersionedRootSignature(&DefaultRootSigDesc, &pRSBlob, nullptr));

    CComPtr<ID3D12RootSignature> pDefaultRootSig;
    pD3DDevice->CreateRootSignature(1, pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(), IID_PPV_ARGS(&pDefaultRootSig));

    m_pDefaultRootSig.Attach(pDefaultRootSig.Detach());
    m_pShaderResourceDescriptorHeap.Attach(pResDH.Detach());
    m_pSamplerDescriptorHeap.Attach(pSamplerDH.Detach());
    m_pRTVDescriptorHeap.Attach(pRTVDH.Detach());
    m_pDSVDescriptorHeap.Attach(pDSVDH.Detach());
    m_pCommandAllocator.Attach(pCA.Detach());
    m_pCommandList.Attach(pCL.Detach());
    m_pCommandQueue.Attach(pCQ.Detach());
    m_pFence.Attach(pFence.Detach());

    // Create UI overlay command list and its own allocator pool
    CComPtr<ID3D12CommandAllocator> pUICA = m_UICommandAllocatorPool.Init(pDevice, D3D12_COMMAND_LIST_TYPE_DIRECT, 4);
    CComPtr<ID3D12GraphicsCommandList> pUICL;
    Gem::ThrowGemError(ResultFromHRESULT(pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pUICA, nullptr, IID_PPV_ARGS(&pUICL))));

    CComPtr<ID3D12GraphicsCommandList7> pUICL7;
    if (SUCCEEDED(pUICL->QueryInterface(IID_PPV_ARGS(&pUICL7))))
        m_pUICommandList7 = pUICL7;

    // Start closed — opened each frame in BeginFrame
    pUICL->Close();
    m_pUICommandAllocator.Attach(pUICA.Detach());
    m_pUICommandList.Attach(pUICL.Detach());

    // Create fixup command list for ECL-time layout bridging
    CComPtr<ID3D12CommandAllocator> pFixupCA = m_FixupCommandAllocatorPool.Init(pDevice, D3D12_COMMAND_LIST_TYPE_DIRECT, 4);
    CComPtr<ID3D12GraphicsCommandList> pFixupCL;
    Gem::ThrowGemError(ResultFromHRESULT(pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pFixupCA, nullptr, IID_PPV_ARGS(&pFixupCL))));

    CComPtr<ID3D12GraphicsCommandList7> pFixupCL7;
    if (SUCCEEDED(pFixupCL->QueryInterface(IID_PPV_ARGS(&pFixupCL7))))
        m_pFixupCommandList7 = pFixupCL7;

    pFixupCL->Close();
    m_pFixupCommandAllocator.Attach(pFixupCA.Detach());
    m_pFixupCommandList.Attach(pFixupCL.Detach());

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
    // Helper: compute fixup barriers to bridge actual→assumed for a task graph's first-use layouts
    auto computeFixupBarriers = [](
        const std::unordered_map<ID3D12Resource*, Canvas::GpuTaskGraphLayoutState>& initialLayouts,
        const std::unordered_map<ID3D12Resource*, SubresourceLayoutState>& committedLayouts,
        const std::unordered_map<ID3D12Resource*, Canvas::GpuTaskGraphLayoutState>* pOverrides)
        -> std::vector<D3D12_TEXTURE_BARRIER>
    {
        std::vector<D3D12_TEXTURE_BARRIER> fixups;
        for (const auto& [pResource, assumedState] : initialLayouts)
        {
            D3D12_BARRIER_LAYOUT assumed = assumedState.UniformLayout.value_or(D3D12_BARRIER_LAYOUT_COMMON);

            // Determine actual layout: overrides (previous CL finals) take priority over committed
            D3D12_BARRIER_LAYOUT actual = D3D12_BARRIER_LAYOUT_COMMON;
            if (pOverrides)
            {
                auto oIt = pOverrides->find(pResource);
                if (oIt != pOverrides->end() && oIt->second.UniformLayout.has_value())
                {
                    actual = oIt->second.UniformLayout.value();
                }
                else
                {
                    auto cIt = committedLayouts.find(pResource);
                    if (cIt != committedLayouts.end() && cIt->second.uniformLayout.has_value())
                        actual = cIt->second.uniformLayout.value();
                }
            }
            else
            {
                auto cIt = committedLayouts.find(pResource);
                if (cIt != committedLayouts.end() && cIt->second.uniformLayout.has_value())
                    actual = cIt->second.uniformLayout.value();
            }

            if (actual != assumed)
            {
                D3D12_TEXTURE_BARRIER tb = {};
                // Fixup: layout-only transition at ECL boundary.
                // Batched with the work CL in the same ECL scope.
                tb.SyncBefore = D3D12_BARRIER_SYNC_NONE;
                tb.SyncAfter = D3D12_BARRIER_SYNC_ALL;
                tb.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
                tb.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
                tb.LayoutBefore = actual;
                tb.LayoutAfter = assumed;
                tb.pResource = pResource;
                tb.Subresources.IndexOrFirstMipLevel = 0xFFFFFFFF;
                tb.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
                fixups.push_back(tb);
            }
        }
        return fixups;
    };

    // Helper: emit fixup barriers into the fixup CL
    auto emitFixups = [this](const std::vector<D3D12_TEXTURE_BARRIER>& fixups)
    {
        ThrowFailedHResult(m_pFixupCommandList->Reset(m_pFixupCommandAllocator, nullptr));
        if (!fixups.empty() && m_pFixupCommandList7)
        {
            D3D12_BARRIER_GROUP group = {};
            group.Type = D3D12_BARRIER_TYPE_TEXTURE;
            group.NumBarriers = static_cast<UINT>(fixups.size());
            group.pTextureBarriers = fixups.data();
            m_pFixupCommandList7->Barrier(1, &group);
        }
        ThrowFailedHResult(m_pFixupCommandList->Close());
    };

    // Helper: update committed state from a task graph's final layouts
    auto commitFinalLayouts = [this](const Canvas::CGpuTaskGraph& graph)
    {
        const auto& finalLayouts = graph.GetFinalLayouts();
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
    };

    // Compute final layouts for all active task graphs
    if (m_TaskGraphActive)
        m_GpuTaskGraph.ComputeFinalLayouts();

    // Close the scene command list
    ThrowFailedHResult(m_pCommandList->Close());

    if (m_UICommandListOpen)
    {
        m_UIGpuTaskGraph.ComputeFinalLayouts();
        ThrowFailedHResult(m_pUICommandList->Close());
        m_UICommandListOpen = false;

        const auto& committed = m_pDevice->m_TextureCurrentLayouts;

        // Scene fixup: committed → scene first-use
        auto sceneFixups = computeFixupBarriers(
            m_GpuTaskGraph.GetInitialLayouts(), committed, nullptr);
        emitFixups(sceneFixups);

        // Submit: [scene_fixup, scene_CL] in same ECL scope
        {
            ID3D12CommandList* pLists[] = { m_pFixupCommandList, m_pCommandList };
            m_pCommandQueue->ExecuteCommandLists(2, pLists);
        }

        // UI fixup: (committed + scene final overlay) → UI first-use
        const auto& sceneFinals = m_GpuTaskGraph.GetFinalLayouts();
        auto uiFixups = computeFixupBarriers(
            m_UIGpuTaskGraph.GetInitialLayouts(), committed, &sceneFinals);

        // Reuse the same fixup CL and allocator — just Reset the CL (not the allocator).
        // The allocator can back multiple CL recordings within the same frame.
        emitFixups(uiFixups);

        // Submit: [ui_fixup, UI_CL] in same ECL scope
        {
            ID3D12CommandList* pLists[] = { m_pFixupCommandList, m_pUICommandList };
            m_pCommandQueue->ExecuteCommandLists(2, pLists);
        }

        // Update committed state: scene first, then UI overrides
        commitFinalLayouts(m_GpuTaskGraph);
        commitFinalLayouts(m_UIGpuTaskGraph);
    }
    else
    {
        // Scene-only: fixup + scene CL
        if (m_TaskGraphActive)
        {
            auto sceneFixups = computeFixupBarriers(
                m_GpuTaskGraph.GetInitialLayouts(), m_pDevice->m_TextureCurrentLayouts, nullptr);
            emitFixups(sceneFixups);

            ID3D12CommandList* pLists[] = { m_pFixupCommandList, m_pCommandList };
            m_pCommandQueue->ExecuteCommandLists(2, pLists);

            commitFinalLayouts(m_GpuTaskGraph);
        }
        else
        {
            m_pCommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&m_pCommandList.p));
        }
    }
    
    // Signal fence
    m_pCommandQueue->Signal(m_pFence, ++m_FenceValue);
    
    // Reset CLs for next frame
    m_pCommandAllocator = m_CommandAllocatorPool.RotateAllocators(this);
    m_pCommandAllocator->Reset();
    ThrowFailedHResult(m_pCommandList->Reset(m_pCommandAllocator, nullptr));

    m_pUICommandAllocator = m_UICommandAllocatorPool.RotateAllocators(this);
    m_pUICommandAllocator->Reset();

    m_pFixupCommandAllocator = m_FixupCommandAllocatorPool.RotateAllocators(this);
    m_pFixupCommandAllocator->Reset();

    m_GpuTaskGraph.Reset();
    m_UIGpuTaskGraph.Reset();
    m_TaskGraphActive = false;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::FlushAndPresent(Canvas::XGfxSwapChain *pSwapChain)
{
    try
    {
        CSwapChain12 *pIntSwapChain = static_cast<CSwapChain12 *>(pSwapChain);

        // Back buffer must be in COMMON for Present.
        // Add as a task to whichever task graph governs the last CL to touch it.
        if (pIntSwapChain->m_BackBufferModified)
        {
            ID3D12Resource* pBackBufferResource = pIntSwapChain->m_pSurface->GetD3DResource();

            if (m_UICommandListOpen)
            {
                // UI CL executes last — declare present transition via UI task graph
                auto presentTask = m_UIGpuTaskGraph.CreateTask("PresentTransition");
                m_UIGpuTaskGraph.DeclareTextureUsage(presentTask, pBackBufferResource,
                    D3D12_BARRIER_LAYOUT_COMMON,
                    D3D12_BARRIER_SYNC_ALL,
                    D3D12_BARRIER_ACCESS_COMMON);
                Canvas::TaskBarriers barriers = m_UIGpuTaskGraph.PrepareTask(presentTask);
                EmitBarriersToCommandList(m_pUICommandList7, barriers);
            }
            else
            {
                // No UI CL — declare on scene CL via scene task graph
                EnsureTaskGraphActive();
                auto presentTask = CreateGpuTask("PresentTransition");
                DeclareGpuTextureUsage(presentTask, pBackBufferResource,
                    D3D12_BARRIER_LAYOUT_COMMON,
                    D3D12_BARRIER_SYNC_ALL,
                    D3D12_BARRIER_ACCESS_COMMON);
                PrepareGpuTask(presentTask);
            }
            
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
void CRenderQueue12::RecordCommandBlock(
    const ResourceUsages& resourceUsages,
    std::function<void(ID3D12GraphicsCommandList*)> recordFunc,
    const char* name)
{
    // Validate resource usage patterns (when diagnostics enabled)
    if (!ValidateResourceUsageNoWriteConflicts(resourceUsages))
    {
        throw Gem::GemError(Gem::Result::InvalidArg);
    }
    
    EnsureTaskGraphActive();
    
    // Create a task and translate ResourceUsages to GpuTask declarations
    auto task = CreateGpuTask(name ? name : "UnnamedCommandTask");
    
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
    CSwapChain12* pIntSwapChain = static_cast<CSwapChain12*>(pSwapChain);
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
    // No initial layouts set — first-use semantics in PrepareTask will auto-record
    // assumed initial layouts. Fixup CL at ECL time bridges committed→assumed.
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
void CRenderQueue12::EmitBarriersToCommandList(ID3D12GraphicsCommandList7* pCL7, const Canvas::TaskBarriers& barriers)
{
    if (!pCL7)
        return;

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

    if (!groups.empty())
    {
        pCL7->Barrier(static_cast<UINT>(groups.size()), groups.data());
    }
}

//================================================================================================
// Shader loading helper
//================================================================================================
static std::vector<uint8_t> LoadShaderBytecode(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return {};
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> bytecode(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytecode.data()), size);
    return bytecode;
}

// Returns the path to the 'shaders/' directory co-located with this DLL.
static std::filesystem::path GetShaderDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    HMODULE hModule = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&LoadShaderBytecode),
        &hModule);
    GetModuleFileNameW(hModule, modulePath, MAX_PATH);
    return std::filesystem::path(modulePath).parent_path() / L"shaders";
}

//================================================================================================
// Depth buffer management
//================================================================================================

//------------------------------------------------------------------------------------------------
void CRenderQueue12::EnsureDepthBuffer(UINT width, UINT height)
{
    if (m_pDepthBuffer && m_DepthBufferWidth == width && m_DepthBufferHeight == height)
        return;
    
    // Create depth buffer surface via the device
    Canvas::GfxSurfaceDesc desc = Canvas::GfxSurfaceDesc::SurfaceDesc2D(
        Canvas::GfxFormat::D32_Float, width, height,
        static_cast<Canvas::GfxSurfaceFlags>(Canvas::SurfaceFlag_DepthStencil));
    
    Gem::TGemPtr<Canvas::XGfxSurface> pSurface;
    Gem::ThrowGemError(m_pDevice->CreateSurface(desc, &pSurface));
    
    Gem::TGemPtr<CSurface12> pDepthSurface;
    pSurface->QueryInterface(&pDepthSurface);
    
    m_pDepthBuffer = pDepthSurface;
    m_DepthBufferWidth = width;
    m_DepthBufferHeight = height;
}

//================================================================================================
// G-buffer management
//================================================================================================

//------------------------------------------------------------------------------------------------
void CRenderQueue12::EnsureGBuffers(UINT width, UINT height)
{
    if (m_pGBufferNormals && m_GBufferWidth == width && m_GBufferHeight == height)
        return;
    
    auto flags = static_cast<Canvas::GfxSurfaceFlags>(
        Canvas::SurfaceFlag_RenderTarget | Canvas::SurfaceFlag_ShaderResource);
    
    // Normals G-buffer
    {
        Canvas::GfxSurfaceDesc desc = Canvas::GfxSurfaceDesc::SurfaceDesc2D(
            m_GBufferNormalsFormat, width, height, flags);
        
        Gem::TGemPtr<Canvas::XGfxSurface> pSurface;
        Gem::ThrowGemError(m_pDevice->CreateSurface(desc, &pSurface));
        
        Gem::TGemPtr<CSurface12> pGBuffer;
        pSurface->QueryInterface(&pGBuffer);
        m_pGBufferNormals = pGBuffer;
    }
    
    // Diffuse color G-buffer
    {
        Canvas::GfxSurfaceDesc desc = Canvas::GfxSurfaceDesc::SurfaceDesc2D(
            m_GBufferDiffuseFormat, width, height, flags);
        
        Gem::TGemPtr<Canvas::XGfxSurface> pSurface;
        Gem::ThrowGemError(m_pDevice->CreateSurface(desc, &pSurface));
        
        Gem::TGemPtr<CSurface12> pGBuffer;
        pSurface->QueryInterface(&pGBuffer);
        m_pGBufferDiffuseColor = pGBuffer;
    }
    
    m_GBufferWidth = width;
    m_GBufferHeight = height;
    
    // Invalidate PSOs since G-buffer formats could have changed
    m_pDefaultPSO.Release();
    m_pCompositePSO.Release();
}

//------------------------------------------------------------------------------------------------
D3D12_CPU_DESCRIPTOR_HANDLE CRenderQueue12::CreateDepthStencilView(CSurface12 *pSurface)
{
    ID3D12Device *pD3DDevice = m_pDevice->GetD3DDevice();
    UINT incSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    UINT slot = m_NextDSVSlot;
    m_NextDSVSlot = (m_NextDSVSlot + 1) % NumDSVDescriptors;
    
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    cpuHandle.ptr = m_pDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + (incSize * slot);
    
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    
    pD3DDevice->CreateDepthStencilView(pSurface->GetD3DResource(), &dsvDesc, cpuHandle);
    return cpuHandle;
}

//================================================================================================
// SRV creation for shader-visible descriptors
//================================================================================================

//------------------------------------------------------------------------------------------------
D3D12_GPU_DESCRIPTOR_HANDLE CRenderQueue12::CreateShaderResourceView(
    ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc)
{
    ID3D12Device *pD3DDevice = m_pDevice->GetD3DDevice();
    UINT incSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    UINT slot = m_NextSRVSlot;
    m_NextSRVSlot = (m_NextSRVSlot + 1) % NumShaderResourceDescriptors;
    
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    cpuHandle.ptr = m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + (incSize * slot);
    
    pD3DDevice->CreateShaderResourceView(pResource, &srvDesc, cpuHandle);
    
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
    gpuHandle.ptr = m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr + (incSize * slot);
    return gpuHandle;
}

//================================================================================================
// PSO creation
//================================================================================================

//------------------------------------------------------------------------------------------------
void CRenderQueue12::EnsureDefaultPSO()
{
    if (m_pDefaultPSO)
        return;
    
    auto shaderDir = GetShaderDirectory();
    auto vsBytecode = LoadShaderBytecode(shaderDir / "VSPrimary.cso");
    auto psBytecode = LoadShaderBytecode(shaderDir / "PSPrimary.cso");
    
    if (vsBytecode.empty() || psBytecode.empty())
    {
        Canvas::LogError(m_pDevice->GetLogger(), "Failed to load shader bytecode (VS: %s, PS: %s)",
            vsBytecode.empty() ? "MISSING" : "OK",
            psBytecode.empty() ? "MISSING" : "OK");
        Gem::ThrowGemError(Gem::Result::Fail);
    }
    
    // Create PSO - geometry pass writes to G-buffer MRTs (normals + diffuse)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_pDefaultRootSig;
    psoDesc.VS = { vsBytecode.data(), vsBytecode.size() };
    psoDesc.PS = { psBytecode.data(), psBytecode.size() };
    
    // No input layout needed - vertices come from structured buffers via SV_VertexID
    psoDesc.InputLayout = { nullptr, 0 };
    
    // Rasterizer state
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
    
    // Blend state (opaque)
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    
    // Depth-stencil state (reverse-Z: near=1.0, far=0.0 → GREATER_EQUAL)
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    
    // MRT: two G-buffer render targets (normals + diffuse color)
    psoDesc.NumRenderTargets = 2;
    psoDesc.RTVFormats[0] = CanvasFormatToDXGIFormat(m_GBufferNormalsFormat);
    psoDesc.RTVFormats[1] = CanvasFormatToDXGIFormat(m_GBufferDiffuseFormat);
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    
    ThrowFailedHResult(m_pDevice->GetD3DDevice()->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_pDefaultPSO)));
    
    Canvas::LogInfo(m_pDevice->GetLogger(), "Geometry pass PSO created (2 MRT G-buffers)");
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::EnsureTextPSO(DXGI_FORMAT rtvFormat)
{
    if (m_pTextPSO && m_TextPSOFormat == rtvFormat)
        return;

    auto shaderDir = GetShaderDirectory();
    auto vsBytecode = LoadShaderBytecode(shaderDir / "VSText.cso");
    auto psBytecode = LoadShaderBytecode(shaderDir / "PSText.cso");

    if (vsBytecode.empty() || psBytecode.empty())
    {
        Canvas::LogError(m_pDevice->GetLogger(), "Failed to load text shader bytecode (VS: %s, PS: %s)",
            vsBytecode.empty() ? "MISSING" : "OK",
            psBytecode.empty() ? "MISSING" : "OK");
        Gem::ThrowGemError(Gem::Result::Fail);
    }

    ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();

    // Text root signature:
    //   Slot 0: Root CBV(b0) – TextScreenConstants (screen width/height)
    //   Slot 1: Root SRV(t0) – StructuredBuffer<TextVertex>
    //   Slot 2: Descriptor table with SRV[1] at t1 – SDFAtlas texture
    //   Static sampler at s0 – linear, clamp
    CD3DX12_STATIC_SAMPLER_DESC linearSampler(
        0,                               // shader register s0
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0,  // SRV[1] at t1, space0
                  D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);

    std::vector<CD3DX12_ROOT_PARAMETER1> textRootParams(3);
    textRootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                                               D3D12_SHADER_VISIBILITY_VERTEX);
    textRootParams[1].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                                               D3D12_SHADER_VISIBILITY_VERTEX);
    textRootParams[2].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC textRootSigDesc(
        static_cast<UINT>(textRootParams.size()),
        textRootParams.data(),
        1, &linearSampler,
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    CComPtr<ID3DBlob> pRSBlob;
    ThrowFailedHResult(D3D12SerializeVersionedRootSignature(&textRootSigDesc, &pRSBlob, nullptr));
    CComPtr<ID3D12RootSignature> pTextRootSig;
    ThrowFailedHResult(pD3DDevice->CreateRootSignature(
        1, pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(), IID_PPV_ARGS(&pTextRootSig)));

    // Alpha blending: src_alpha / inv_src_alpha
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend = {};
    rtBlend.BlendEnable           = TRUE;
    rtBlend.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    rtBlend.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp               = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha         = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = pTextRootSig;
    psoDesc.VS = { vsBytecode.data(), vsBytecode.size() };
    psoDesc.PS = { psBytecode.data(), psBytecode.size() };
    psoDesc.InputLayout              = { nullptr, 0 };       // vertices via SRV + SV_VertexID
    psoDesc.RasterizerState          = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // quads may have either winding
    psoDesc.BlendState               = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.BlendState.RenderTarget[0] = rtBlend;
    psoDesc.DepthStencilState        = {};                   // depth test + write disabled
    psoDesc.SampleMask               = UINT_MAX;
    psoDesc.PrimitiveTopologyType    = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets         = 1;
    psoDesc.RTVFormats[0]            = rtvFormat;
    psoDesc.DSVFormat                = DXGI_FORMAT_UNKNOWN;  // no depth buffer needed for text
    psoDesc.SampleDesc.Count         = 1;
    psoDesc.SampleDesc.Quality       = 0;

    CComPtr<ID3D12PipelineState> pTextPSO;
    ThrowFailedHResult(pD3DDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pTextPSO)));

    m_pTextRootSig.Attach(pTextRootSig.Detach());
    m_pTextPSO.Attach(pTextPSO.Detach());
    m_TextPSOFormat = rtvFormat;

    Canvas::LogInfo(m_pDevice->GetLogger(), "Text PSO created successfully");
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::EnsureCompositePSO(DXGI_FORMAT rtvFormat)
{
    if (m_pCompositePSO)
        return;

    auto shaderDir = GetShaderDirectory();
    auto vsBytecode = LoadShaderBytecode(shaderDir / "VSFullscreen.cso");
    auto psBytecode = LoadShaderBytecode(shaderDir / "PSComposite.cso");

    if (vsBytecode.empty() || psBytecode.empty())
    {
        Canvas::LogError(m_pDevice->GetLogger(), "Failed to load composite shader bytecode (VS: %s, PS: %s)",
            vsBytecode.empty() ? "MISSING" : "OK",
            psBytecode.empty() ? "MISSING" : "OK");
        Gem::ThrowGemError(Gem::Result::Fail);
    }

    ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();

    // Composite root signature:
    //   Slot 0: Root CBV (b0) — lighting / per-frame constants
    //   Slot 1: Descriptor table with SRV[2] at t0-t1 — G-buffer textures
    //   Static sampler s0: point, clamp (exact texel fetch)
    CD3DX12_STATIC_SAMPLER_DESC pointSampler(
        0,                                      // shader register s0
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0,  // SRV[2] at t0-t1, space0
                  D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);

    std::vector<CD3DX12_ROOT_PARAMETER1> compositeRootParams(2);
    compositeRootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                                                    D3D12_SHADER_VISIBILITY_PIXEL);
    compositeRootParams[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC compositeRootSigDesc(
        static_cast<UINT>(compositeRootParams.size()),
        compositeRootParams.data(),
        1, &pointSampler,
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    CComPtr<ID3DBlob> pRSBlob;
    ThrowFailedHResult(D3D12SerializeVersionedRootSignature(&compositeRootSigDesc, &pRSBlob, nullptr));
    CComPtr<ID3D12RootSignature> pCompositeRootSig;
    ThrowFailedHResult(pD3DDevice->CreateRootSignature(
        1, pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(), IID_PPV_ARGS(&pCompositeRootSig)));

    // PSO: fullscreen pass, no depth, single render target (back buffer)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = pCompositeRootSig;
    psoDesc.VS = { vsBytecode.data(), vsBytecode.size() };
    psoDesc.PS = { psBytecode.data(), psBytecode.size() };
    psoDesc.InputLayout              = { nullptr, 0 };
    psoDesc.RasterizerState          = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState               = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState        = {};  // No depth test for fullscreen pass
    psoDesc.SampleMask               = UINT_MAX;
    psoDesc.PrimitiveTopologyType    = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets         = 1;
    psoDesc.RTVFormats[0]            = rtvFormat;
    psoDesc.DSVFormat                = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count         = 1;
    psoDesc.SampleDesc.Quality       = 0;

    CComPtr<ID3D12PipelineState> pCompositePSO;
    ThrowFailedHResult(pD3DDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pCompositePSO)));

    m_pCompositeRootSig.Attach(pCompositeRootSig.Detach());
    m_pCompositePSO.Attach(pCompositePSO.Detach());

    Canvas::LogInfo(m_pDevice->GetLogger(), "Composite PSO created successfully");
}

//================================================================================================
// Texture upload
//================================================================================================

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::UploadTextureRegion(
    Canvas::XGfxSurface *pDstSurface,
    uint32_t dstX, uint32_t dstY,
    uint32_t width, uint32_t height,
    const void *pData,
    uint32_t srcRowPitch,
    Canvas::GfxRenderContext context)
{
    if (!pDstSurface || !pData || width == 0 || height == 0)
        return Gem::Result::BadPointer;

    try
    {
        auto pDst = static_cast<CSurface12*>(pDstSurface);
        ID3D12Resource* pDstResource = pDst->GetD3DResource();

        // Compute aligned row pitch for the staging buffer
        // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT = 256 bytes
        constexpr uint32_t kPitchAlign = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        uint32_t alignedRowPitch = (srcRowPitch + kPitchAlign - 1) & ~(kPitchAlign - 1);
        uint64_t stagingSize = static_cast<uint64_t>(alignedRowPitch) * height;

        // Allocate staging buffer (power-of-2 size satisfies D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT=512)
        Canvas::GfxSuballocation stagingAlloc;
        Gem::ThrowGemError(m_pDevice->AllocateHostWriteRegion(stagingSize, stagingAlloc));

        // Copy rows from source into staging buffer (may need re-striding)
        auto pStagingBuf = static_cast<CBuffer12*>(stagingAlloc.pBuffer.Get());
        ID3D12Resource* pStagingResource = pStagingBuf->GetD3DResource();

        void* pMapped = nullptr;
        ThrowFailedHResult(pStagingResource->Map(0, nullptr, &pMapped));
        for (uint32_t row = 0; row < height; ++row)
        {
            const uint8_t* pSrcRow = static_cast<const uint8_t*>(pData) + row * srcRowPitch;
            uint8_t* pDstRow = static_cast<uint8_t*>(pMapped) + stagingAlloc.Offset + row * alignedRowPitch;
            memcpy(pDstRow, pSrcRow, srcRowPitch);
        }
        pStagingResource->Unmap(0, nullptr);

        // Prepare copy location descriptors
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource                          = pStagingResource;
        src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset             = stagingAlloc.Offset;
        src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8_UNORM;
        src.PlacedFootprint.Footprint.Width    = width;
        src.PlacedFootprint.Footprint.Height   = height;
        src.PlacedFootprint.Footprint.Depth    = 1;
        src.PlacedFootprint.Footprint.RowPitch = alignedRowPitch;

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource        = pDstResource;
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_BOX srcBox = { 0, 0, 0, width, height, 1 };

        if (context == Canvas::GfxRenderContext::UI && m_UICommandListOpen)
        {
            // UI context: declare copy via UI task graph, record into UI CL
            auto copyTask = m_UIGpuTaskGraph.CreateTask("UploadTextureRegion_UI");
            m_UIGpuTaskGraph.DeclareTextureUsage(copyTask, pDstResource,
                D3D12_BARRIER_LAYOUT_COMMON,
                D3D12_BARRIER_SYNC_COPY,
                D3D12_BARRIER_ACCESS_COPY_DEST);
            Canvas::TaskBarriers copyBarriers = m_UIGpuTaskGraph.PrepareTask(copyTask);
            EmitBarriersToCommandList(m_pUICommandList7, copyBarriers);

            m_pUICommandList->CopyTextureRegion(&dst, dstX, dstY, 0, &src, &srcBox);
        }
        else
        {
            // Scene context (or outside a frame): use scene command list via task graph
            ResourceUsageBuilder usages;
            usages.TextureAsCopyDest(pDstResource);

            RecordCommandBlock(
                usages.Build(),
                [&src, &dst, &srcBox, dstX, dstY]
                (ID3D12GraphicsCommandList* cmdList)
                {
                    cmdList->CopyTextureRegion(&dst, dstX, dstY, 0, &src, &srcBox);
                },
                "UploadTextureRegion");
        }

        RetireUploadAllocation(stagingAlloc);
        return Gem::Result::Success;
    }
    catch (Gem::GemError& e)
    {
        return e.Result();
    }
    catch (_com_error& e)
    {
        return ResultFromHRESULT(e.Error());
    }
}

//================================================================================================
// Frame rendering methods
//================================================================================================

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::BeginFrame(
    Canvas::XGfxSwapChain *pSwapChain)
{
    try
    {
        CSwapChain12 *pIntSwapChain = static_cast<CSwapChain12*>(pSwapChain);
        CSurface12 *pBackBuffer = pIntSwapChain->m_pSurface;
        ID3D12Resource *pBackBufferResource = pBackBuffer->GetD3DResource();
        
        // Get back buffer dimensions
        D3D12_RESOURCE_DESC bbDesc = pBackBufferResource->GetDesc();
        UINT width = static_cast<UINT>(bbDesc.Width);
        UINT height = bbDesc.Height;
        
        // Ensure depth buffer matches back buffer size
        EnsureDepthBuffer(width, height);
        
        // Ensure G-buffer render targets match back buffer size
        EnsureGBuffers(width, height);
        
        // Ensure PSOs are created
        EnsureDefaultPSO();
        EnsureCompositePSO(bbDesc.Format);
        
        EnsureTaskGraphActive();
        
        // Transition G-buffers to render target and depth buffer to depth-stencil write
        // (back buffer stays in COMMON until the composition pass)
        ID3D12Resource* pNormalsResource = m_pGBufferNormals->GetD3DResource();
        ID3D12Resource* pDiffuseResource = m_pGBufferDiffuseColor->GetD3DResource();

        auto task = CreateGpuTask("BeginFrame_GBufferPass");
        DeclareGpuTextureUsage(task, pNormalsResource,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        DeclareGpuTextureUsage(task, pDiffuseResource,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        DeclareGpuTextureUsage(task, m_pDepthBuffer->GetD3DResource(),
            D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
            D3D12_BARRIER_SYNC_DEPTH_STENCIL,
            D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
        PrepareGpuTask(task);
        
        // Create RTVs for G-buffers and DSV
        D3D12_CPU_DESCRIPTOR_HANDLE gbufferRTVs[2];
        gbufferRTVs[0] = CreateRenderTargetView(m_pGBufferNormals, 0, 0, 0);
        gbufferRTVs[1] = CreateRenderTargetView(m_pGBufferDiffuseColor, 0, 0, 0);
        m_CurrentDSV = CreateDepthStencilView(m_pDepthBuffer);
        
        // Clear G-buffers (alpha=0 marks empty pixels for the composition pass)
        const float clearGBuffer[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_pCommandList->ClearRenderTargetView(gbufferRTVs[0], clearGBuffer, 0, nullptr);
        m_pCommandList->ClearRenderTargetView(gbufferRTVs[1], clearGBuffer, 0, nullptr);
        m_pCommandList->ClearDepthStencilView(m_CurrentDSV, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr); // Reverse-Z: 0.0 = far plane
        
        // Set G-buffer render targets for the geometry pass
        m_pCommandList->OMSetRenderTargets(2, gbufferRTVs, FALSE, &m_CurrentDSV);
        
        // Store back buffer RTV for later composition pass
        m_CurrentRTV = CreateRenderTargetView(pBackBuffer, 0, 0, 0);
        
        // Set viewport and scissor
        D3D12_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        m_pCommandList->RSSetViewports(1, &viewport);
        
        D3D12_RECT scissor = {};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<LONG>(width);
        scissor.bottom = static_cast<LONG>(height);
        m_pCommandList->RSSetScissorRects(1, &scissor);
        
        // Set PSO and root signature for geometry pass
        m_pCommandList->SetPipelineState(m_pDefaultPSO);
        m_pCommandList->SetGraphicsRootSignature(m_pDefaultRootSig);
        
        // Set descriptor heaps
        ID3D12DescriptorHeap* heaps[] = { m_pShaderResourceDescriptorHeap, m_pSamplerDescriptorHeap };
        m_pCommandList->SetDescriptorHeaps(2, heaps);
        
        // Set topology
        m_pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        
        m_pCurrentSwapChain = pIntSwapChain;
        pIntSwapChain->m_BackBufferModified = true;

        // Open UI overlay command list for text/HUD recording.
        ThrowFailedHResult(m_pUICommandList->Reset(m_pUICommandAllocator, nullptr));
        m_UICommandListOpen = true;

        // Initialize UI task graph — no initial layouts needed.
        // First-use semantics will auto-record assumed layouts.
        m_UIGpuTaskGraph.Reset();

        // Declare back buffer as RENDER_TARGET for UI draws.
        // First-use: no barrier emitted (the fixup CL at ECL time will bridge).
        auto uiBeginTask = m_UIGpuTaskGraph.CreateTask("UIBeginFrame");
        m_UIGpuTaskGraph.DeclareTextureUsage(uiBeginTask, pBackBufferResource,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        Canvas::TaskBarriers uiBeginBarriers = m_UIGpuTaskGraph.PrepareTask(uiBeginTask);
        EmitBarriersToCommandList(m_pUICommandList7, uiBeginBarriers);

        // Set up UI command list state: back buffer RTV, viewport, scissor, descriptor heaps
        m_pUICommandList->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, nullptr);
        m_pUICommandList->RSSetViewports(1, &viewport);
        m_pUICommandList->RSSetScissorRects(1, &scissor);

        ID3D12DescriptorHeap* uiHeaps[] = { m_pShaderResourceDescriptorHeap, m_pSamplerDescriptorHeap };
        m_pUICommandList->SetDescriptorHeaps(2, uiHeaps);
        m_pUICommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
    catch (Gem::GemError &e)
    {
        return e.Result();
    }
    catch (_com_error &e)
    {
        return ResultFromHRESULT(e.Error());
    }
    
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::DrawMesh(
    Canvas::XGfxMeshData *pMeshData,
    const Canvas::GfxPerObjectConstants &objectConstants)
{
    try
    {
        if (!pMeshData || !m_pCurrentSwapChain)
            return Gem::Result::InvalidArg;
        
        auto pMesh = static_cast<CMeshData12*>(pMeshData);
        
        // Get position and normal vertex buffer entries
        auto pPosEntry = pMesh->GetVertexBuffer(0, Canvas::GfxVertexBufferType::Position);
        auto pNormEntry = pMesh->GetVertexBuffer(0, Canvas::GfxVertexBufferType::Normal);
        
        if (!pPosEntry || !pPosEntry->pBuffer)
            return Gem::Result::InvalidArg;
        
        auto pPosBuf = static_cast<CBuffer12*>(pPosEntry->pBuffer.Get());
        CBuffer12* pNormBuf = (pNormEntry && pNormEntry->pBuffer)
            ? static_cast<CBuffer12*>(pNormEntry->pBuffer.Get())
            : nullptr;
        
        // Upload per-object constants to upload heap
        // CBVs require 256-byte aligned BufferLocation (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)
        constexpr uint64_t cbAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        Canvas::GfxSuballocation cbAlloc;
        Gem::ThrowGemError(m_pDevice->AllocateHostWriteRegion(
            (sizeof(Canvas::GfxPerObjectConstants) + cbAlignment - 1) & ~(cbAlignment - 1), cbAlloc));
        
        auto pHostBuf = static_cast<CBuffer12*>(cbAlloc.pBuffer.Get());
        ID3D12Resource* pHostResource = pHostBuf->GetD3DResource();
        
        void* pMapped = nullptr;
        ThrowFailedHResult(pHostResource->Map(0, nullptr, &pMapped));
        memcpy(static_cast<uint8_t*>(pMapped) + cbAlloc.Offset,
               &objectConstants, sizeof(Canvas::GfxPerObjectConstants));
        pHostResource->Unmap(0, nullptr);
        
        // Create CBV for per-object constants in descriptor table
        // Descriptor table (slot 3) layout: CBV[2] at b1, SRV[4] at t1, UAV[2] at u1
        // We need to create a contiguous block of descriptors for the table
        ID3D12Device *pD3DDevice = m_pDevice->GetD3DDevice();
        UINT incSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        
        // Allocate a contiguous block of 8 descriptors (2 CBV + 4 SRV + 2 UAV).
        // Wrap to slot 0 if the block would straddle the ring boundary to avoid
        // writing past the end of the heap.
        if (m_NextSRVSlot + 8 > NumShaderResourceDescriptors)
            m_NextSRVSlot = 0;
        UINT baseSlot = m_NextSRVSlot;
        m_NextSRVSlot += 8;
        
        D3D12_CPU_DESCRIPTOR_HANDLE baseCpuHandle;
        baseCpuHandle.ptr = m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + (incSize * baseSlot);
        
        D3D12_GPU_DESCRIPTOR_HANDLE baseGpuHandle;
        baseGpuHandle.ptr = m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr + (incSize * baseSlot);
        
        // Slot 0 of table: CBV for per-object constants (b1)
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = pHostResource->GetGPUVirtualAddress() + cbAlloc.Offset;
        cbvDesc.SizeInBytes = static_cast<UINT>((sizeof(Canvas::GfxPerObjectConstants) + 255) & ~255); // 256-byte aligned
        pD3DDevice->CreateConstantBufferView(&cbvDesc, baseCpuHandle);
        
        // Slot 1 of table: CBV[1] placeholder (b2) - null
        D3D12_CPU_DESCRIPTOR_HANDLE cbv1Handle = { baseCpuHandle.ptr + incSize };
        D3D12_CONSTANT_BUFFER_VIEW_DESC nullCbvDesc = {};
        pD3DDevice->CreateConstantBufferView(&nullCbvDesc, cbv1Handle);
        
        // Slots 2-5 of table: SRV[4] at t1-t4
        // Slot 2: normals (t1), slots 3-5: null SRVs (t2-t4)
        // All must be initialized since the descriptor range is STATIC
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
        nullSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        
        if (pNormBuf)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE normSrvHandle = { baseCpuHandle.ptr + 2 * incSize };
            D3D12_RESOURCE_DESC normDesc = pNormBuf->GetD3DResource()->GetDesc();
            UINT numNormals = static_cast<UINT>(normDesc.Width / sizeof(Canvas::Math::FloatVector4));
            
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = numNormals;
            srvDesc.Buffer.StructureByteStride = sizeof(Canvas::Math::FloatVector4);
            
            pD3DDevice->CreateShaderResourceView(pNormBuf->GetD3DResource(), &srvDesc, normSrvHandle);
        }
        else
        {
            pD3DDevice->CreateShaderResourceView(nullptr, &nullSrvDesc, { baseCpuHandle.ptr + 2 * incSize });
        }
        
        // Null SRVs for t2-t4 (slots 3-5)
        for (UINT i = 3; i <= 5; ++i)
            pD3DDevice->CreateShaderResourceView(nullptr, &nullSrvDesc, { baseCpuHandle.ptr + i * incSize });
        
        // Null UAVs for u1-u2 (slots 6-7)
        D3D12_UNORDERED_ACCESS_VIEW_DESC nullUavDesc = {};
        nullUavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        nullUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        for (UINT i = 6; i <= 7; ++i)
            pD3DDevice->CreateUnorderedAccessView(nullptr, nullptr, &nullUavDesc, { baseCpuHandle.ptr + i * incSize });
        
        // Set root SRV (slot 1) for positions (t0)
        m_pCommandList->SetGraphicsRootShaderResourceView(1,
            pPosBuf->GetD3DResource()->GetGPUVirtualAddress());
        
        // Set descriptor table (slot 3)
        m_pCommandList->SetGraphicsRootDescriptorTable(3, baseGpuHandle);
        
        RetireUploadAllocation(cbAlloc);
        
        // Determine vertex count from position buffer size
        D3D12_RESOURCE_DESC posDesc = pPosBuf->GetD3DResource()->GetDesc();
        UINT vertexCount = static_cast<UINT>(posDesc.Width / sizeof(Canvas::Math::FloatVector4));
        
        m_pCommandList->DrawInstanced(vertexCount, 1, 0, 0);
    }
    catch (Gem::GemError &e)
    {
        return e.Result();
    }
    catch (_com_error &e)
    {
        return ResultFromHRESULT(e.Error());
    }
    
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::DrawText(
    const void *pVertexData,
    uint32_t vertexCount,
    Canvas::XGfxSurface *pGlyphAtlas,
    const Canvas::Math::FloatVector4 &screenOffset)
{
    (void)screenOffset;

    if (!pVertexData || vertexCount == 0 || !pGlyphAtlas || !m_pCurrentSwapChain)
    {
        Canvas::LogError(m_pDevice->GetLogger(), "DrawText: invalid arguments (pVertexData=%p, vertexCount=%u, pGlyphAtlas=%p, swapChain=%p)",
            pVertexData, vertexCount, pGlyphAtlas, m_pCurrentSwapChain);
        return Gem::Result::InvalidArg;
    }

    Canvas::GfxSuballocation vertexAlloc{};
    Canvas::GfxSuballocation cbAlloc{};

    try
    {
        DXGI_FORMAT rtvFormat = m_pCurrentSwapChain->m_pSurface->GetD3DResource()->GetDesc().Format;
        EnsureTextPSO(rtvFormat);

        auto pAtlas = static_cast<CSurface12*>(pGlyphAtlas);

        // Declare atlas as SHADER_RESOURCE via UI task graph.
        // Barriers are emitted into the UI CL based on the assumed initial layout.
        // The fixup CL at ECL time bridges any discrepancy with actual state.
        auto atlasTask = m_UIGpuTaskGraph.CreateTask("DrawText_Atlas");
        m_UIGpuTaskGraph.DeclareTextureUsage(atlasTask, pAtlas->GetD3DResource(),
            D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_PIXEL_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        Canvas::TaskBarriers atlasBarriers = m_UIGpuTaskGraph.PrepareTask(atlasTask);
        EmitBarriersToCommandList(m_pUICommandList7, atlasBarriers);

        // Vertex buffer: TextVertex = float3 Position + float2 TexCoord + uint Color = 24 bytes
        constexpr uint64_t kTextVertexSize = sizeof(float) * 5 + sizeof(uint32_t); // 24
        uint64_t vertexBufferSize = vertexCount * kTextVertexSize;
        Gem::ThrowGemError(m_pDevice->AllocateHostWriteRegion(vertexBufferSize, vertexAlloc));

        auto pVertexBuf  = static_cast<CBuffer12*>(vertexAlloc.pBuffer.Get());
        ID3D12Resource*  pVertexResource = pVertexBuf->GetD3DResource();
        void* pMapped = nullptr;
        ThrowFailedHResult(pVertexResource->Map(0, nullptr, &pMapped));
        memcpy(static_cast<uint8_t*>(pMapped) + vertexAlloc.Offset, pVertexData, vertexBufferSize);
        pVertexResource->Unmap(0, nullptr);

        // Screen constants CBV: float2 ScreenSize + float2 padding = 16 bytes, padded to 256
        constexpr uint64_t kCBVSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        Gem::ThrowGemError(m_pDevice->AllocateHostWriteRegion(kCBVSize, cbAlloc));

        auto pCBBuf     = static_cast<CBuffer12*>(cbAlloc.pBuffer.Get());
        ID3D12Resource* pCBResource = pCBBuf->GetD3DResource();
        float screenConsts[4] = { static_cast<float>(m_DepthBufferWidth), static_cast<float>(m_DepthBufferHeight), 0.0f, 0.0f };
        void* pCBMapped = nullptr;
        ThrowFailedHResult(pCBResource->Map(0, nullptr, &pCBMapped));
        memcpy(static_cast<uint8_t*>(pCBMapped) + cbAlloc.Offset, screenConsts, sizeof(screenConsts));
        pCBResource->Unmap(0, nullptr);

        // One atlas SRV descriptor in the SRV heap
        ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();
        UINT incSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        UINT srvSlot = m_NextSRVSlot;
        m_NextSRVSlot = (m_NextSRVSlot + 1) % NumShaderResourceDescriptors;

        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle;
        srvCpuHandle.ptr = m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + incSize * srvSlot;
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle;
        srvGpuHandle.ptr = m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr + incSize * srvSlot;

        // Atlas SRV (maps to t1 in PSText.hlsl via the descriptor table)
        pD3DDevice->CreateShaderResourceView(pAtlas->GetD3DResource(), nullptr, srvCpuHandle);

        // Record draw commands on the UI command list
        m_pUICommandList->SetGraphicsRootSignature(m_pTextRootSig);
        m_pUICommandList->SetPipelineState(m_pTextPSO);

        // Slot 0: screen constants CBV (b0)
        m_pUICommandList->SetGraphicsRootConstantBufferView(0,
            pCBResource->GetGPUVirtualAddress() + cbAlloc.Offset);

        // Slot 1: vertex StructuredBuffer SRV (t0)
        m_pUICommandList->SetGraphicsRootShaderResourceView(1,
            pVertexResource->GetGPUVirtualAddress() + vertexAlloc.Offset);

        // Slot 2: atlas texture descriptor table (t1)
        m_pUICommandList->SetGraphicsRootDescriptorTable(2, srvGpuHandle);

        m_pUICommandList->DrawInstanced(vertexCount, 1, 0, 0);

        RetireUploadAllocation(vertexAlloc);
        RetireUploadAllocation(cbAlloc);
    }
    catch (Gem::GemError& e)
    {
        if (vertexAlloc.pBuffer) RetireUploadAllocation(vertexAlloc);
        if (cbAlloc.pBuffer)     RetireUploadAllocation(cbAlloc);
        Canvas::LogError(m_pDevice->GetLogger(), "DrawText failed: result=0x%08X", (unsigned)e.Result());
        return e.Result();
    }
    catch (_com_error& e)
    {
        if (vertexAlloc.pBuffer) RetireUploadAllocation(vertexAlloc);
        if (cbAlloc.pBuffer)     RetireUploadAllocation(cbAlloc);
        Canvas::LogError(m_pDevice->GetLogger(), "DrawText failed: HRESULT=0x%08X", (unsigned)e.Error());
        return ResultFromHRESULT(e.Error());
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::SubmitForRender(Canvas::XSceneGraphElement *pElement)
{
    if (!pElement)
        return Gem::Result::InvalidArg;

    m_RenderableQueue.push_back(pElement);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CRenderQueue12::SetActiveCamera(Canvas::XCamera *pCamera)
{
    m_pActiveCamera = pCamera;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::EndFrame()
{
    try
    {
        // Build per-frame constants from the active camera
        Canvas::GfxPerFrameConstants frameConstants = {};
        
        if (m_pActiveCamera)
        {
            frameConstants.ViewProj = m_pActiveCamera->GetViewProjectionMatrix();
            auto *pCameraNode = m_pActiveCamera->GetAttachedNode();
            if (pCameraNode)
                frameConstants.CameraWorldPos = pCameraNode->GetGlobalTranslation();
        }

        // Default lighting — TODO: gather from scene lights
        Canvas::Math::FloatVector4 sunDir(0.3f, 0.5f, 0.7f, 0.0f);
        frameConstants.SunDirection = sunDir.Normalize();
        frameConstants.SunColor = Canvas::Math::FloatVector4(1.0f, 0.95f, 0.85f, 0.0f);
        frameConstants.AmbientLight = Canvas::Math::FloatVector4(0.15f, 0.15f, 0.2f, 0.0f);

        // Upload per-frame constants and bind to root CBV (slot 0, register b0)
        constexpr uint64_t cbAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        Canvas::GfxSuballocation cbAlloc;
        Gem::ThrowGemError(m_pDevice->AllocateHostWriteRegion(
            (sizeof(Canvas::GfxPerFrameConstants) + cbAlignment - 1) & ~(cbAlignment - 1), cbAlloc));
        
        auto pHostBuf = static_cast<CBuffer12*>(cbAlloc.pBuffer.Get());
        ID3D12Resource* pHostResource = pHostBuf->GetD3DResource();
        
        void* pMapped = nullptr;
        ThrowFailedHResult(pHostResource->Map(0, nullptr, &pMapped));
        memcpy(static_cast<uint8_t*>(pMapped) + cbAlloc.Offset,
               &frameConstants, sizeof(Canvas::GfxPerFrameConstants));
        pHostResource->Unmap(0, nullptr);
        
        m_pCommandList->SetGraphicsRootConstantBufferView(0,
            pHostResource->GetGPUVirtualAddress() + cbAlloc.Offset);

        // Drain the renderable queue — geometry pass renders to G-buffers
        for (auto *pElement : m_RenderableQueue)
        {
            Gem::ThrowGemError(pElement->DispatchForRender(this));
        }
        m_RenderableQueue.clear();

        //==========================================================================================
        // Composition pass: read G-buffers, perform deferred lighting, write to back buffer
        //==========================================================================================
        {
            ID3D12Resource* pNormalsResource = m_pGBufferNormals->GetD3DResource();
            ID3D12Resource* pDiffuseResource = m_pGBufferDiffuseColor->GetD3DResource();
            CSurface12* pBackBuffer = m_pCurrentSwapChain->m_pSurface;
            ID3D12Resource* pBackBufferResource = pBackBuffer->GetD3DResource();

            // Transition G-buffers to shader resource and back buffer to render target
            auto compositeTask = CreateGpuTask("CompositePass");
            DeclareGpuTextureUsage(compositeTask, pNormalsResource,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            DeclareGpuTextureUsage(compositeTask, pDiffuseResource,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            DeclareGpuTextureUsage(compositeTask, pBackBufferResource,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            PrepareGpuTask(compositeTask);

            // Clear back buffer
            const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            m_pCommandList->ClearRenderTargetView(m_CurrentRTV, clearColor, 0, nullptr);

            // Set back buffer as render target (no depth)
            m_pCommandList->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, nullptr);

            // Switch to composite pipeline
            m_pCommandList->SetGraphicsRootSignature(m_pCompositeRootSig);
            m_pCommandList->SetPipelineState(m_pCompositePSO);

            // Re-bind descriptor heaps (required after root signature change)
            ID3D12DescriptorHeap* heaps[] = { m_pShaderResourceDescriptorHeap, m_pSamplerDescriptorHeap };
            m_pCommandList->SetDescriptorHeaps(2, heaps);

            // Slot 0: per-frame constants CBV (reuse the same upload for lighting data)
            m_pCommandList->SetGraphicsRootConstantBufferView(0,
                pHostResource->GetGPUVirtualAddress() + cbAlloc.Offset);

            // Slot 1: G-buffer SRV descriptor table (t0 = normals, t1 = diffuse)
            ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();
            UINT incSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            // Allocate 2 contiguous SRV descriptors
            if (m_NextSRVSlot + 2 > NumShaderResourceDescriptors)
                m_NextSRVSlot = 0;
            UINT baseSlot = m_NextSRVSlot;
            m_NextSRVSlot += 2;

            D3D12_CPU_DESCRIPTOR_HANDLE baseCpuHandle;
            baseCpuHandle.ptr = m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + (incSize * baseSlot);
            D3D12_GPU_DESCRIPTOR_HANDLE baseGpuHandle;
            baseGpuHandle.ptr = m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr + (incSize * baseSlot);

            // t0: normals G-buffer SRV
            pD3DDevice->CreateShaderResourceView(pNormalsResource, nullptr, baseCpuHandle);
            // t1: diffuse color G-buffer SRV
            D3D12_CPU_DESCRIPTOR_HANDLE diffuseSrvHandle = { baseCpuHandle.ptr + incSize };
            pD3DDevice->CreateShaderResourceView(pDiffuseResource, nullptr, diffuseSrvHandle);

            m_pCommandList->SetGraphicsRootDescriptorTable(1, baseGpuHandle);

            // Draw fullscreen triangle
            m_pCommandList->DrawInstanced(3, 1, 0, 0);
        }

        RetireUploadAllocation(cbAlloc);
    }
    catch (Gem::GemError &e)
    {
        m_RenderableQueue.clear();
        m_pCurrentSwapChain = nullptr;
        m_pActiveCamera = nullptr;
        return e.Result();
    }

    m_pCurrentSwapChain = nullptr;
    m_pActiveCamera = nullptr;
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::AddGpuTaskDependency(Canvas::GpuTaskHandle task, Canvas::GpuTaskHandle dependency)
{
    m_GpuTaskGraph.AddExplicitDependency(task, dependency);
}
