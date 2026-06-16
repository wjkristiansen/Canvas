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
#include "UITextElement12.h"

#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>

namespace
{
    // Global light culling threshold.
    // 0 disables intensity-based culling until an explicit tuned value is provided.
    constexpr float kDefaultLightCullThreshold = 0.0f;

    // Computes the maximum distance at which a light contributes above the cull threshold,
    // given its attenuation coefficients and authored range.  The result is stored in
    // HlslLight::AttenuationAndRange.w so the shader can early-out without recomputing it.
    float ComputeLightCutoffDistance(
        float peakIntensity,
        float c, float l, float q,
        float range,
        float threshold)
    {
        if (threshold <= 0.0f)
            return range > 0.0f ? range : 1e20f;

        if (peakIntensity <= threshold)
            return 0.0f;

        c = (std::max)(c, 0.0f);
        l = (std::max)(l, 0.0f);
        q = (std::max)(q, 0.0f);

        float targetDenom = peakIntensity / threshold;

        float cutoff = 1e20f;
        if (q > 1e-8f)
        {
            float disc = l * l - 4.0f * q * (c - targetDenom);
            if (disc < 0.0f)
                return 0.0f;
            cutoff = (std::max)(0.0f, (-l + std::sqrt(disc)) / (2.0f * q));
        }
        else if (l > 1e-8f)
        {
            cutoff = (std::max)(0.0f, (targetDenom - c) / l);
        }
        else if (c > 1e-8f)
        {
            cutoff = (peakIntensity / c > threshold) ? 1e20f : 0.0f;
        }
        else
        {
            cutoff = 0.0f;
        }

        if (range > 0.0f)
            cutoff = (std::min)(cutoff, range);

        return cutoff;
    }
}

// Verify Canvas::LightType enum values match the HLSL LIGHT_* defines (HlslTypes.h).
static_assert(static_cast<uint32_t>(Canvas::LightType::Ambient)     == LIGHT_AMBIENT,     "LightType::Ambient mismatch");
static_assert(static_cast<uint32_t>(Canvas::LightType::Point)       == LIGHT_POINT,       "LightType::Point mismatch");
static_assert(static_cast<uint32_t>(Canvas::LightType::Directional) == LIGHT_DIRECTIONAL, "LightType::Directional mismatch");
static_assert(static_cast<uint32_t>(Canvas::LightType::Spot)        == LIGHT_SPOT,        "LightType::Spot mismatch");
static_assert(static_cast<uint32_t>(Canvas::LightType::Area)        == LIGHT_AREA,        "LightType::Area mismatch");

//------------------------------------------------------------------------------------------------
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

    // Initialize allocator pool (grows on demand as allocators are needed)
    m_AllocatorPool.Init(pDevice, D3D12_COMMAND_LIST_TYPE_DIRECT);

    // Initialize task graphs - all start with work CLs closed
    m_GpuTaskGraph.Init(pD3DDevice, pCQ, &m_AllocatorPool);
    m_UIGpuTaskGraph.Init(pD3DDevice, pCQ, &m_AllocatorPool);
    m_PresentGpuTaskGraph.Init(pD3DDevice, pCQ, &m_AllocatorPool);

    // Name the task graph command lists for D3D12 debug tools
    if (name)
    {
        SetD3D12DebugName(m_GpuTaskGraph.GetWorkCommandList(),        name, "Scene_WorkCL");
        SetD3D12DebugName(m_GpuTaskGraph.GetFixupCommandList(),       name, "Scene_FixupCL");
        SetD3D12DebugName(m_UIGpuTaskGraph.GetWorkCommandList(),      name, "UI_WorkCL");
        SetD3D12DebugName(m_UIGpuTaskGraph.GetFixupCommandList(),     name, "UI_FixupCL");
        SetD3D12DebugName(m_PresentGpuTaskGraph.GetWorkCommandList(), name, "Present_WorkCL");
        SetD3D12DebugName(m_PresentGpuTaskGraph.GetFixupCommandList(),name, "Present_FixupCL");
    }

    // The shader-visible CBV/SRV/UAV heap is owned by the device and shared across render
    // queues so per-resource descriptors (mesh streams, material textures) have a stable home.
    // This queue caches a ref to it and binds it; its transient SRV ring carves the heap's
    // upper partition (the lower partition is the device's persistent allocator).
    CComPtr<ID3D12DescriptorHeap> pSamplerDH;
    D3D12_DESCRIPTOR_HEAP_DESC DHDesc = {};
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

    // The default root descriptor table is laid out as follows:
    //  CBV[2]  @ b1, b2  (data static)
    //  SRV[9]  @ t1..t9  (data static) - t1=normals, t2=UV0, t3=tangents,
    //                    t4=albedo, t5=normalMap, t6=emissive,
    //                    t7=roughness, t8=metallic, t9=ambient occlusion
    //  UAV[2]  @ u1, u2  (descriptor static)
    //
    // A single static sampler s0 (LinearWrap, anisotropic mip filter) covers
    // all material texture sampling.

    // Default root signature layout:
    //   Param 0: Root CBV  b0        - per-frame constants
    //   Param 1: Root SRV  t0        - vertex positions
    //   Param 2: Root UAV  u0        - (unused)
    //   Param 3: Descriptor table
    //     CBV[2]  b1, b2             - per-object CB + null
    //     SRV[11] t1..t11            - normals(t1), UV0(t2), tangents(t3),
    //                                  albedo(t4..t9), boneIndices(t10), boneWeights(t11)
    //     UAV[2]  u1, u2             - null
    //   Param 4: Root SRV  t12       - bone matrix palette (per-draw, uploaded to upload ring)
    std::vector<CD3DX12_DESCRIPTOR_RANGE1> DefaultDescriptorRanges(3);
    DefaultDescriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2,  1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0);
    DefaultDescriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 11, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 2);
    DefaultDescriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2,  1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 13);

    std::vector<CD3DX12_ROOT_PARAMETER1> DefaultRootParams(5);
    DefaultRootParams[0].InitAsConstantBufferView(0,  0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,   D3D12_SHADER_VISIBILITY_ALL);
    DefaultRootParams[1].InitAsShaderResourceView(0,  0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,   D3D12_SHADER_VISIBILITY_ALL);
    DefaultRootParams[2].InitAsUnorderedAccessView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE,          D3D12_SHADER_VISIBILITY_ALL);
    DefaultRootParams[3].InitAsDescriptorTable(static_cast<UINT>(DefaultDescriptorRanges.size()), DefaultDescriptorRanges.data(), D3D12_SHADER_VISIBILITY_ALL);
    DefaultRootParams[4].InitAsShaderResourceView(12, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_VERTEX);

    CD3DX12_STATIC_SAMPLER_DESC DefaultStaticSamplers[1] = {};
    DefaultStaticSamplers[0].Init(
        0,                                          // s0
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC DefaultRootSigDesc(
        5U, DefaultRootParams.data(),
        _countof(DefaultStaticSamplers), DefaultStaticSamplers,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    CComPtr<ID3DBlob> pRSBlob;
    ThrowFailedHResult(D3D12SerializeVersionedRootSignature(&DefaultRootSigDesc, &pRSBlob, nullptr));

    CComPtr<ID3D12RootSignature> pDefaultRootSig;
    pD3DDevice->CreateRootSignature(1, pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(), IID_PPV_ARGS(&pDefaultRootSig));

    m_pDefaultRootSig.Attach(pDefaultRootSig.Detach());
    m_pShaderResourceDescriptorHeap = m_pDevice->GetShaderResourceDescriptorHeap();  // shared, ref-counted
    m_pSamplerDescriptorHeap.Attach(pSamplerDH.Detach());
    m_pRTVDescriptorHeap.Attach(pRTVDH.Detach());
    m_pDSVDescriptorHeap.Attach(pDSVDH.Detach());
    m_pCommandQueue.Attach(pCQ.Detach());
    m_pFence.Attach(pFence.Detach());

    // Propagate Canvas element name to D3D12 objects for debug tools
    if (name)
    {
        SetD3D12DebugName(m_pCommandQueue,                name, "CommandQueue");
        SetD3D12DebugName(m_pFence,                       name, "Fence");
        // m_pShaderResourceDescriptorHeap is the device-owned shared heap; it is named once
        // at device init, not per queue.
        SetD3D12DebugName(m_pSamplerDescriptorHeap,       name, "Sampler_DescHeap");
        SetD3D12DebugName(m_pRTVDescriptorHeap,           name, "RTV_DescHeap");
        SetD3D12DebugName(m_pDSVDescriptorHeap,           name, "DSV_DescHeap");
        SetD3D12DebugName(m_pDefaultRootSig,              name, "DefaultRootSig");
    }

    m_CbvSrvUavIncrement = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_SamplerIncrement   = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    m_RtvIncrement       = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_DsvIncrement       = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    m_DescriptorHeapsArray[0] = m_pShaderResourceDescriptorHeap;
    m_DescriptorHeapsArray[1] = m_pSamplerDescriptorHeap;

    // Per-queue upload ring (1 MB initial, grows on demand).
    m_UploadRing.Initialize(pDevice, 1 * 1024 * 1024);

    // Fence-protected descriptor ring allocators.  They gate slot reuse on this queue's fence
    // so wrap-around cannot overwrite descriptors still referenced by in-flight GPU work.
    // RTV/DSV heaps are per-queue and fully ring-managed from slot 0.  The SRV ring carves
    // only the transient upper partition of the shared device heap; the lower partition is the
    // device's persistent per-resource allocator.  Claiming the transient partition throws if
    // a second render queue is created on this device (see CDevice12::AcquireTransientSrvRange).
    m_RTVRing.Initialize(0, NumRTVDescriptors);
    m_DSVRing.Initialize(0, NumDSVDescriptors);
    UINT srvBase = 0, srvCount = 0;
    m_pDevice->AcquireTransientSrvRange(srvBase, srvCount);
    m_SRVRing.Initialize(srvBase, srvCount);

    // Register this queue's fence with the device-level resource manager so
    // pooled buffers and deferred releases can be tracked queue-agnostically.
    m_TimelineId = pDevice->GetResourceManager().RegisterTimeline(m_pFence);

    // Pre-allocate per-frame queues to avoid repeated heap reallocations
    m_RenderableQueue.reserve(128);
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
        Gem::ThrowGemError(TGfxElement<CSwapChain12>::CreateAndRegister<CSwapChain12>(&pSwapChain, GetCanvas(), hWnd, Windowed, this, dxgiFormat, NumBuffers, "SwapChain"));
        
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
    UINT slot = AllocateRTVSlots(1);
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_pRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), slot, m_RtvIncrement);
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
    // Gate this frame's work on any pending device-level upload work (e.g.
    // CCopyQueue buffer copies for newly-created mesh data).  The token is
    // queue-agnostic so the same primitive will serve a future ECL graph.
    if (auto token = m_pDevice->EnsureUploadsRetired())
    {
        if (auto* pFence = m_pDevice->GetResourceManager().GetTimelineFence(token->TimelineId))
            m_pCommandQueue->Wait(pFence, token->Value);
    }

    // Dispatch order: scene -> UI
    if (m_TaskGraphActive)
        m_GpuTaskGraph.Dispatch();

    if (m_UICommandListOpen)
    {
        m_UICommandListOpen = false;
        m_UIGpuTaskGraph.Dispatch();
    }
    
    // Signal fence for scene + UI work (allocator rotation depends on this)
    m_pCommandQueue->Signal(m_pFence, ++m_FenceValue);

    // Record ring buffer usage for this frame
    m_UploadRing.MarkSubmissionEnd(m_FenceValue);

    // Tag this frame's descriptor slots with the fence just signalled; they become
    // reclaimable once the GPU passes it.  All RTV/DSV (BeginFrame) and SRV (EndFrame)
    // allocations for this frame are consumed by the scene + UI work signalled above.
    m_RTVRing.MarkSubmissionEnd(m_FenceValue);
    m_DSVRing.MarkSubmissionEnd(m_FenceValue);
    m_SRVRing.MarkSubmissionEnd(m_FenceValue);

    // Reset scene and UI graphs for next frame
    UINT64 completedFenceValue = m_pFence->GetCompletedValue();
    m_GpuTaskGraph.Reset(m_FenceValue, completedFenceValue);
    m_UIGpuTaskGraph.Reset(m_FenceValue, completedFenceValue);

    m_TaskGraphActive = false;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::FlushAndPresent(Canvas::XGfxSwapChain *pSwapChain)
{
    try
    {
        CSwapChain12 *pIntSwapChain = static_cast<CSwapChain12 *>(pSwapChain);

        // Dispatch scene and UI graphs first
        Flush();

        // Back buffer -> COMMON for Present. Always dispatched last via the present graph.
        if (pIntSwapChain->m_BackBufferModified)
        {
            CSurface12* pBackBuffer = pIntSwapChain->m_pSurface;

            // Open present graph work CL and insert the transition task
            m_PresentGpuTaskGraph.GetWorkCommandList()->Reset(
                m_PresentGpuTaskGraph.GetWorkAllocator(), nullptr);
            auto& presentTask = m_PresentGpuTaskGraph.CreateTask("PresentTransition");
            m_PresentGpuTaskGraph.DeclareTextureUsage(presentTask, pBackBuffer,
                D3D12_BARRIER_LAYOUT_COMMON,
                D3D12_BARRIER_SYNC_NONE,
                D3D12_BARRIER_ACCESS_NO_ACCESS);
            m_PresentGpuTaskGraph.InsertTask(presentTask);
            m_PresentGpuTaskGraph.Dispatch();
            
            pIntSwapChain->m_BackBufferModified = false;
        }

        // Signal fence after all graphs have been submitted
        m_pCommandQueue->Signal(m_pFence, ++m_FenceValue);
        m_PresentGpuTaskGraph.Reset(m_FenceValue, m_pFence->GetCompletedValue());

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

    // GPU is idle - drain any remaining deferred resources.
    ProcessCompletedWork();

    // Unregister this queue's timeline from the device-level resource manager.
    // Drains and drops any retired buffers / deferred refs owned by this timeline.
    // The shared bucketed pool stays alive - it is owned by the device and torn
    // down when the device is destroyed (or by the last surviving queue's release
    // chain via CDevice12::~CDevice12).
    if (m_TimelineId != FenceToken::kInvalidTimelineId)
    {
        m_pDevice->GetResourceManager().UnregisterTimeline(m_TimelineId);
        m_TimelineId = FenceToken::kInvalidTimelineId;
    }

    // Release the per-queue upload ring now that the GPU is idle.
    m_UploadRing.Shutdown();

    // Release the shared heap's transient partition so a future render queue on this device
    // can claim it.
    m_pDevice->ReleaseTransientSrvRange();
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
    auto& task = CreateGpuTask(name ? name : "UnnamedCommandTask");
    
    for (const auto& texUsage : resourceUsages.TextureUsages)
    {
        if (!texUsage.IsValid()) continue;
        DeclareGpuTextureUsage(task, texUsage.pSurface,
            texUsage.RequiredLayout,
            texUsage.SyncForUsage,
            texUsage.AccessForUsage,
            texUsage.Subresources);
    }
    
    for (const auto& bufUsage : resourceUsages.BufferUsages)
    {
        if (!bufUsage.IsValid()) continue;
        DeclareGpuBufferUsage(task, bufUsage.pBuffer,
            bufUsage.SyncForUsage,
            bufUsage.AccessForUsage,
            bufUsage.Offset,
            bufUsage.Size);
    }
    
    task.RecordFunc = std::move(recordFunc);
    m_GpuTaskGraph.InsertTask(task);
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
CRenderQueue12::ResourceStateSnapshot CRenderQueue12::GetResourceState(CSurface12* pSurface) const
{
    ResourceStateSnapshot snapshot;
    
    if (pSurface)
    {
        // Read committed layout directly from the surface wrapper
        snapshot.UniformLayout = pSurface->GetCurSubresourceLayout(0);
    }
    
    return snapshot;
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::ProcessCompletedWork()
{
    UINT64 completedValue = m_pFence->GetCompletedValue();
    
    // Reclaim upload ring buffer space in bulk
    m_UploadRing.Reclaim(completedValue);

    // Release descriptor slots whose referencing GPU work has retired.
    m_RTVRing.Reclaim(completedValue);
    m_DSVRing.Reclaim(completedValue);
    m_SRVRing.Reclaim(completedValue);

    // Reclaim pooled buffers and (later) deferred releases across all timelines.
    m_pDevice->GetResourceManager().Reclaim();

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
        // Open the scene work CL for recording
        m_GpuTaskGraph.GetWorkCommandList()->Reset(m_GpuTaskGraph.GetWorkAllocator(), nullptr);
        m_TaskGraphActive = true;
    }
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
Canvas::CGpuTask& CRenderQueue12::CreateGpuTask(const char* name)
{
    return m_GpuTaskGraph.CreateTask(name);
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::DeclareGpuTextureUsage(
    Canvas::CGpuTask& task,
    CSurface12* pSurface,
    D3D12_BARRIER_LAYOUT requiredLayout,
    D3D12_BARRIER_SYNC sync,
    D3D12_BARRIER_ACCESS access,
    UINT subresources)
{
    m_GpuTaskGraph.DeclareTextureUsage(task, pSurface, requiredLayout, sync, access, subresources);
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::DeclareGpuBufferUsage(
    Canvas::CGpuTask& task,
    CBuffer12* pBuffer,
    D3D12_BARRIER_SYNC sync,
    D3D12_BARRIER_ACCESS access,
    UINT64 offset,
    UINT64 size)
{
    m_GpuTaskGraph.DeclareBufferUsage(task, pBuffer, sync, access, offset, size);
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
void CRenderQueue12::EnsureShadowAtlas()
{
    if (m_pShadowAtlas)
        return;

    const UINT atlasSize = kShadowAtlasDefaultSize;

    // Typed D32_Float with both DepthStencil and ShaderResource flags --
    // D3D12 allows binding the same resource as a DSV during the shadow
    // pass and as an SRV during the composite.  The SRV format will be
    // R32_Float (the typed depth format itself is not a valid SRV
    // format) and is created on demand at composite time.
    auto flags = static_cast<Canvas::GfxSurfaceFlags>(
        Canvas::SurfaceFlag_DepthStencil | Canvas::SurfaceFlag_ShaderResource);
    Canvas::GfxSurfaceDesc desc = Canvas::GfxSurfaceDesc::SurfaceDesc2D(
        Canvas::GfxFormat::D32_Float, atlasSize, atlasSize, flags);

    Gem::TGemPtr<Canvas::XGfxSurface> pSurface;
    Gem::ThrowGemError(m_pDevice->CreateSurface(desc, &pSurface));

    Gem::TGemPtr<CSurface12> pShadowSurface;
    pSurface->QueryInterface(&pShadowSurface);

    m_pShadowAtlas             = pShadowSurface;
    m_ShadowAtlasSize          = atlasSize;
    m_ShadowAtlasTilesPerSide  = kShadowAtlasTilesPerSide;

    // Note: m_ShadowAtlasDSV is allocated PER FRAME (in EndFrame's shadow
    // pass block), not here.  The DSV descriptor heap is round-robin and
    // would otherwise eventually wrap and overwrite our cached slot,
    // silently rebinding m_ShadowAtlasDSV to whatever resource came next.
}

//------------------------------------------------------------------------------------------------
CRenderQueue12::ShadowAtlasTile CRenderQueue12::AllocateShadowTile(UINT preferredResolution)
{
    ShadowAtlasTile tile = {};
    tile.Valid = false;

    if (!m_pShadowAtlas || m_ShadowAtlasTilesPerSide == 0)
        return tile;
    if (m_NextShadowTileIndex >= kShadowAtlasMaxTiles)
        return tile;

    const UINT cellSize = m_ShadowAtlasSize / m_ShadowAtlasTilesPerSide;
    // v1 ignores per-light requested resolution beyond the fixed cell:
    // each tile is exactly cellSize px on a side.  Smaller requests still
    // get the cell so we don't waste atlas memory tracking sub-tile slack.
    (void)preferredResolution;

    const UINT idx  = m_NextShadowTileIndex++;
    const UINT row  = idx / m_ShadowAtlasTilesPerSide;
    const UINT col  = idx % m_ShadowAtlasTilesPerSide;
    const UINT pixX = col * cellSize;
    const UINT pixY = row * cellSize;

    const float invAtlas = 1.0f / static_cast<float>(m_ShadowAtlasSize);
    tile.Valid       = true;
    tile.PixelX      = pixX;
    tile.PixelY      = pixY;
    tile.PixelSize   = cellSize;
    tile.AtlasRectUV = Canvas::Math::FloatVector4(
        static_cast<float>(pixX)     * invAtlas,
        static_cast<float>(pixY)     * invAtlas,
        static_cast<float>(cellSize) * invAtlas,
        static_cast<float>(cellSize) * invAtlas);
    return tile;
}

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
    m_GBufferDescriptorsDirty = true;
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

    // World position G-buffer
    {
        Canvas::GfxSurfaceDesc desc = Canvas::GfxSurfaceDesc::SurfaceDesc2D(
            m_GBufferWorldPosFormat, width, height, flags);

        Gem::TGemPtr<Canvas::XGfxSurface> pSurface;
        Gem::ThrowGemError(m_pDevice->CreateSurface(desc, &pSurface));

        Gem::TGemPtr<CSurface12> pGBuffer;
        pSurface->QueryInterface(&pGBuffer);
        m_pGBufferWorldPos = pGBuffer;
    }

    // PBR G-buffer (R=Roughness, G=Metallic, B=AO, A=spare)
    {
        Canvas::GfxSurfaceDesc desc = Canvas::GfxSurfaceDesc::SurfaceDesc2D(
            m_GBufferPBRFormat, width, height, flags);

        Gem::TGemPtr<Canvas::XGfxSurface> pSurface;
        Gem::ThrowGemError(m_pDevice->CreateSurface(desc, &pSurface));

        Gem::TGemPtr<CSurface12> pGBuffer;
        pSurface->QueryInterface(&pGBuffer);
        m_pGBufferPBR = pGBuffer;
    }

    // Emissive G-buffer (RGB linear, R11G11B10_FLOAT)
    {
        Canvas::GfxSurfaceDesc desc = Canvas::GfxSurfaceDesc::SurfaceDesc2D(
            m_GBufferEmissiveFormat, width, height, flags);

        Gem::TGemPtr<Canvas::XGfxSurface> pSurface;
        Gem::ThrowGemError(m_pDevice->CreateSurface(desc, &pSurface));

        Gem::TGemPtr<CSurface12> pGBuffer;
        pSurface->QueryInterface(&pGBuffer);
        m_pGBufferEmissive = pGBuffer;
    }
    
    m_GBufferWidth = width;
    m_GBufferHeight = height;
    
    // Invalidate PSOs and cached descriptors since G-buffer formats could have changed
    m_pDefaultPSO.Release();
    m_pCompositePSO.Release();
    m_GBufferDescriptorsDirty = true;
}

//------------------------------------------------------------------------------------------------
D3D12_CPU_DESCRIPTOR_HANDLE CRenderQueue12::CreateDepthStencilView(CSurface12 *pSurface)
{
    ID3D12Device *pD3DDevice = m_pDevice->GetD3DDevice();
    UINT slot = AllocateDSVSlots(1);

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_pDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), slot, m_DsvIncrement);
    
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
    UINT slot = AllocateSRVSlots(1);

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), slot, m_CbvSrvUavIncrement);
    
    pD3DDevice->CreateShaderResourceView(pResource, &srvDesc, cpuHandle);
    
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), slot, m_CbvSrvUavIncrement);
    return gpuHandle;
}

//================================================================================================
// PSO creation
//================================================================================================

//------------------------------------------------------------------------------------------------
void CRenderQueue12::EnsureDefaultPSOWireframe()
{
    if (m_pDefaultPSOWireframe)
        return;
    EnsureDefaultPSO();  // share root sig + shaders with the solid variant

    auto shaderDir = GetShaderDirectory();
    auto vsBytecode = LoadShaderBytecode(shaderDir / "VSPrimary.cso");
    auto psBytecode = LoadShaderBytecode(shaderDir / "PSPrimary.cso");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_pDefaultRootSig;
    psoDesc.VS = { vsBytecode.data(), vsBytecode.size() };
    psoDesc.PS = { psBytecode.data(), psBytecode.size() };
    psoDesc.InputLayout = { nullptr, 0 };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;     // see both sides while debugging
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 5;
    psoDesc.RTVFormats[0] = CanvasFormatToDXGIFormat(m_GBufferNormalsFormat);
    psoDesc.RTVFormats[1] = CanvasFormatToDXGIFormat(m_GBufferDiffuseFormat);
    psoDesc.RTVFormats[2] = CanvasFormatToDXGIFormat(m_GBufferWorldPosFormat);
    psoDesc.RTVFormats[3] = CanvasFormatToDXGIFormat(m_GBufferPBRFormat);
    psoDesc.RTVFormats[4] = CanvasFormatToDXGIFormat(m_GBufferEmissiveFormat);
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    ThrowFailedHResult(m_pDevice->GetD3DDevice()->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_pDefaultPSOWireframe)));
    SetD3D12DebugName(m_pDefaultPSOWireframe, GetName(), "DefaultPSO_Wireframe");

    Canvas::LogInfo(m_pDevice->GetLogger(), "Geometry pass wireframe PSO created");
}

GEMMETHODIMP_(void) CRenderQueue12::SetGeometryWireframe(bool wireframe)
{
    m_GeometryWireframe = wireframe;
}

GEMMETHODIMP_(bool) CRenderQueue12::GetGeometryWireframe() const
{
    return m_GeometryWireframe;
}

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
    
    // Create PSO - geometry pass writes to G-buffer MRTs (normals + diffuse + world pos)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_pDefaultRootSig;
    psoDesc.VS = { vsBytecode.data(), vsBytecode.size() };
    psoDesc.PS = { psBytecode.data(), psBytecode.size() };
    
    // No input layout needed - vertices come from structured buffers via SV_VertexID
    psoDesc.InputLayout = { nullptr, 0 };
    
    // Rasterizer state.  Engine winding contract:
    //   Author CCW-front meshes in Canvas world (RHS standard).
    //   The world->view basis change is the engine's single internal
    //   RHS->LHS bridge; it has determinant -1, so authored CCW-front
    //   triangles arrive in clip space as CCW.  FrontCounterClockwise=TRUE
    //   tells the rasterizer that those clip-space CCW triangles are
    //   front-facing, matching the actual post-view winding produced by
    //   valid mesh data.  See CanvasMath.hpp PerspectiveReverseZ and
    //   CanvasCore/Camera.cpp for the full pipeline documentation.
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;

    // Blend state (opaque)
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    
    // Depth-stencil state (reverse-Z: near=1.0, far=0.0 -> GREATER_EQUAL)
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    
    // MRT: five G-buffer render targets - Normals, Diffuse, WorldPos, PBR, Emissive
    psoDesc.NumRenderTargets = 5;
    psoDesc.RTVFormats[0] = CanvasFormatToDXGIFormat(m_GBufferNormalsFormat);
    psoDesc.RTVFormats[1] = CanvasFormatToDXGIFormat(m_GBufferDiffuseFormat);
    psoDesc.RTVFormats[2] = CanvasFormatToDXGIFormat(m_GBufferWorldPosFormat);
    psoDesc.RTVFormats[3] = CanvasFormatToDXGIFormat(m_GBufferPBRFormat);
    psoDesc.RTVFormats[4] = CanvasFormatToDXGIFormat(m_GBufferEmissiveFormat);
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    
    ThrowFailedHResult(m_pDevice->GetD3DDevice()->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_pDefaultPSO)));
    SetD3D12DebugName(m_pDefaultPSO, GetName(), "DefaultPSO");
    
    Canvas::LogInfo(m_pDevice->GetLogger(), "Geometry pass PSO created (5 MRT G-buffers)");
}

//------------------------------------------------------------------------------------------------
// Displaced-mesh pipeline (GPU tessellation): VS + HS + DS + PS over a quad patch
// list. Root signature exposes:
//   b0: per-frame constants (shared with the rest of the engine)
//   b1: per-instance constants (HlslDisplacedConstants)
//   t0: displacement-map SRV (HS + DS visible: HS reads coarse mip for curvature,
//       DS reads mip 0 for vertex displacement)
//   t1..t3: material atlas SRVs (PS visible: albedo / AO / roughness)
//   s0: static linear-clamp sampler shared by all stages
//------------------------------------------------------------------------------------------------
static void BuildDisplacedPSODesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC &out,
                                const std::vector<uint8_t> &vsBytecode,
                                const std::vector<uint8_t> &hsBytecode,
                                const std::vector<uint8_t> &dsBytecode,
                                const std::vector<uint8_t> &psBytecode,
                                ID3D12RootSignature *pRootSig,
                                DXGI_FORMAT normalsFmt,
                                DXGI_FORMAT diffuseFmt,
                                DXGI_FORMAT worldPosFmt,
                                DXGI_FORMAT pbrFmt,
                                DXGI_FORMAT emissiveFmt,
                                bool wireframe)
{
    out = {};
    out.pRootSignature = pRootSig;
    out.VS = { vsBytecode.data(), vsBytecode.size() };
    out.HS = { hsBytecode.data(), hsBytecode.size() };
    out.DS = { dsBytecode.data(), dsBytecode.size() };
    out.PS = { psBytecode.data(), psBytecode.size() };
    out.InputLayout = { nullptr, 0 };  // CP positions / UVs sourced from t4..t5 SRVs indexed by SV_VertexID

    out.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    // See EnsureDefaultPSO for the full winding-contract documentation.
    // Canvas authors CCW-front meshes in RHS world; the world->view bridge
    // has det -1; the resulting clip-space CCW triangles are front-facing.
    // Callers feeding this PSO bake a v -> worldY flip into the patch's
    // per-CP UVs (image-top renders at high worldY); paired with the
    // HSDisplaced triangle_ccw output topology that keeps the resulting
    // world geometry CCW-front and consistent with this FCC=TRUE chain.
    // The wireframe variant disables culling entirely so wireframe
    // debugging shows both faces of a displaced patch.
    out.RasterizerState.FrontCounterClockwise = TRUE;
    if (wireframe)
    {
        out.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        out.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }

    out.BlendState        = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    out.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    out.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    out.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    out.SampleMask = UINT_MAX;
    out.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    out.NumRenderTargets = 5;
    out.RTVFormats[0] = normalsFmt;
    out.RTVFormats[1] = diffuseFmt;
    out.RTVFormats[2] = worldPosFmt;
    out.RTVFormats[3] = pbrFmt;
    out.RTVFormats[4] = emissiveFmt;
    out.SampleDesc.Count   = 1;
    out.SampleDesc.Quality = 0;
}

void CRenderQueue12::EnsureDisplacedPSO()
{
    if (m_pDisplacedPSO)
        return;

    ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();

    // ---------- Root signature ----------
    if (!m_pDisplacedRootSig)
    {
        // Layout:
        //   t0     -- displacement map  (HS curvature + DS lift)         -- ALL
        //   t1..t3 -- albedo / AO / roughness atlases                    -- PIXEL
        //   t4..t6 -- per-CP position + UV0 + normal StructuredBuffer SRVs -- VERTEX
        // All ranges are DATA_STATIC; the render queue's
        // FinalizeUploadAsShaderResource gate ensures their layout
        // transitions have retired before bind time.
        CD3DX12_DESCRIPTOR_RANGE1 mapRange;
        mapRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);  // t0

        CD3DX12_DESCRIPTOR_RANGE1 materialRange;
        materialRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);  // t1..t3

        CD3DX12_DESCRIPTOR_RANGE1 cpRange;
        cpRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 4, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);  // t4..t6

        std::vector<CD3DX12_ROOT_PARAMETER1> rootParams(5);
        rootParams[0].InitAsConstantBufferView(0, 0,
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
            D3D12_SHADER_VISIBILITY_ALL);   // b0 PerFrame
        rootParams[1].InitAsConstantBufferView(1, 0,
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
            D3D12_SHADER_VISIBILITY_ALL);   // b1 PerTile
        rootParams[2].InitAsDescriptorTable(1, &mapRange,
            D3D12_SHADER_VISIBILITY_ALL);      // t0 (HS reads for LOD, DS reads for displacement lift)
        rootParams[3].InitAsDescriptorTable(1, &materialRange,
            D3D12_SHADER_VISIBILITY_PIXEL);    // t1..t3
        rootParams[4].InitAsDescriptorTable(1, &cpRange,
            D3D12_SHADER_VISIBILITY_VERTEX);   // t4..t5 CP streams

        // One static sampler serves both stages. Both the displacement map
        // (DS) and the pre-baked tile-sized material atlases (PS) want
        // linear-filtered clamp-addressed sampling, so a single ALL-visible
        // sampler keeps the root sig small.
        CD3DX12_STATIC_SAMPLER_DESC sampler;
        sampler.Init(
            0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc(
            static_cast<UINT>(rootParams.size()), rootParams.data(),
            1, &sampler,
            D3D12_ROOT_SIGNATURE_FLAG_NONE);

        CComPtr<ID3DBlob> pRSBlob;
        ThrowFailedHResult(D3D12SerializeVersionedRootSignature(&rsDesc, &pRSBlob, nullptr));
        ThrowFailedHResult(pD3DDevice->CreateRootSignature(1,
            pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(),
            IID_PPV_ARGS(&m_pDisplacedRootSig)));
        SetD3D12DebugName(m_pDisplacedRootSig, GetName(), "DisplacedRootSig");
    }

    // ---------- Solid PSO ----------
    auto shaderDir = GetShaderDirectory();
    auto vs = LoadShaderBytecode(shaderDir / "VSDisplaced.cso");
    auto hs = LoadShaderBytecode(shaderDir / "HSDisplaced.cso");
    auto ds = LoadShaderBytecode(shaderDir / "DSDisplaced.cso");
    auto ps = LoadShaderBytecode(shaderDir / "PSDisplaced.cso");
    if (vs.empty() || hs.empty() || ds.empty() || ps.empty())
    {
        Canvas::LogError(m_pDevice->GetLogger(),
            "Failed to load displaced-mesh shader bytecode (VS:%s HS:%s DS:%s PS:%s)",
            vs.empty() ? "MISSING" : "OK", hs.empty() ? "MISSING" : "OK",
            ds.empty() ? "MISSING" : "OK", ps.empty() ? "MISSING" : "OK");
        Gem::ThrowGemError(Gem::Result::Fail);
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    BuildDisplacedPSODesc(psoDesc, vs, hs, ds, ps, m_pDisplacedRootSig,
        CanvasFormatToDXGIFormat(m_GBufferNormalsFormat),
        CanvasFormatToDXGIFormat(m_GBufferDiffuseFormat),
        CanvasFormatToDXGIFormat(m_GBufferWorldPosFormat),
        CanvasFormatToDXGIFormat(m_GBufferPBRFormat),
        CanvasFormatToDXGIFormat(m_GBufferEmissiveFormat),
        /*wireframe*/ false);
    ThrowFailedHResult(pD3DDevice->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_pDisplacedPSO)));
    SetD3D12DebugName(m_pDisplacedPSO, GetName(), "DisplacedPSO");

    Canvas::LogInfo(m_pDevice->GetLogger(), "Displaced-mesh PSO created (VS+HS+DS+PS, quad patches)");
}

void CRenderQueue12::EnsureDisplacedPSOWireframe()
{
    if (m_pDisplacedPSOWireframe)
        return;
    EnsureDisplacedPSO();  // share root sig + shaders

    auto shaderDir = GetShaderDirectory();
    auto vs = LoadShaderBytecode(shaderDir / "VSDisplaced.cso");
    auto hs = LoadShaderBytecode(shaderDir / "HSDisplaced.cso");
    auto ds = LoadShaderBytecode(shaderDir / "DSDisplaced.cso");
    auto ps = LoadShaderBytecode(shaderDir / "PSDisplaced.cso");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    BuildDisplacedPSODesc(psoDesc, vs, hs, ds, ps, m_pDisplacedRootSig,
        CanvasFormatToDXGIFormat(m_GBufferNormalsFormat),
        CanvasFormatToDXGIFormat(m_GBufferDiffuseFormat),
        CanvasFormatToDXGIFormat(m_GBufferWorldPosFormat),
        CanvasFormatToDXGIFormat(m_GBufferPBRFormat),
        CanvasFormatToDXGIFormat(m_GBufferEmissiveFormat),
        /*wireframe*/ true);
    ThrowFailedHResult(m_pDevice->GetD3DDevice()->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_pDisplacedPSOWireframe)));
    SetD3D12DebugName(m_pDisplacedPSOWireframe, GetName(), "DisplacedPSO_Wireframe");
}

//------------------------------------------------------------------------------------------------
// Depth-only displaced shadow PSO.  Shares the displaced VS + HS bytecode
// with the geometry-pass PSO so shadow tessellation matches receiver
// tessellation; pairs them with DSDisplacedShadow (writes only
// SV_Position with the per-light ShadowViewProj at b2) and no PS, so the
// rasterizer writes only depth into the bound shadow-atlas tile.
//
// Root signature:
//   b0: HlslPerFrameConstants (HS reads CameraWorldPos for LOD)
//   b1: HlslDisplacedConstants (per-tile world transform + displacement-map params)
//   b2: HlslShadowConstants    (world->shadow-clip matrix for this light)
//   t0: displacement-map SRV   (HS coarse-mip LOD + DS displacement lift)
//   s0: static linear-clamp sampler
//
// Rasterizer state bakes conservative caster-side depth bias defaults
// (DepthBias + SlopeScaledDepthBias).  Per-light caster bias from
// XLight::SetShadowDepthBias is not honoured in v1 -- only the
// receiver-side constant + normal-offset terms (consumed by the
// composite via HlslLight) vary per light.  Per-light caster bias
// would require either dynamic RSSetDepthBias (D3D12 12_2+) or one
// PSO variant per light; both are deferred.
void CRenderQueue12::EnsureDisplacedShadowPSO()
{
    if (m_pDisplacedShadowPSO)
        return;

    ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();

    // ---------- Root signature ----------
    if (!m_pDisplacedShadowRootSig)
    {
        CD3DX12_DESCRIPTOR_RANGE1 mapRange;
        mapRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);  // t0

        CD3DX12_DESCRIPTOR_RANGE1 cpRange;
        cpRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 4, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);  // t4..t6

        std::vector<CD3DX12_ROOT_PARAMETER1> rootParams(5);
        rootParams[0].InitAsConstantBufferView(0, 0,
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
            D3D12_SHADER_VISIBILITY_ALL);          // b0 PerFrame (HS LOD)
        rootParams[1].InitAsConstantBufferView(1, 0,
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
            D3D12_SHADER_VISIBILITY_ALL);          // b1 PerTile
        rootParams[2].InitAsConstantBufferView(2, 0,
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
            D3D12_SHADER_VISIBILITY_DOMAIN);       // b2 Shadow ViewProj
        rootParams[3].InitAsDescriptorTable(1, &mapRange,
            D3D12_SHADER_VISIBILITY_ALL);          // t0 displacement map
        rootParams[4].InitAsDescriptorTable(1, &cpRange,
            D3D12_SHADER_VISIBILITY_VERTEX);       // t4..t6 CP streams

        CD3DX12_STATIC_SAMPLER_DESC sampler;
        sampler.Init(
            0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc(
            static_cast<UINT>(rootParams.size()), rootParams.data(),
            1, &sampler,
            D3D12_ROOT_SIGNATURE_FLAG_NONE);

        CComPtr<ID3DBlob> pRSBlob;
        ThrowFailedHResult(D3D12SerializeVersionedRootSignature(&rsDesc, &pRSBlob, nullptr));
        ThrowFailedHResult(pD3DDevice->CreateRootSignature(1,
            pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(),
            IID_PPV_ARGS(&m_pDisplacedShadowRootSig)));
        SetD3D12DebugName(m_pDisplacedShadowRootSig, GetName(), "DisplacedShadowRootSig");
    }

    auto shaderDir = GetShaderDirectory();
    auto vs = LoadShaderBytecode(shaderDir / "VSDisplaced.cso");
    auto hs = LoadShaderBytecode(shaderDir / "HSDisplaced.cso");
    auto ds = LoadShaderBytecode(shaderDir / "DSDisplacedShadow.cso");
    if (vs.empty() || hs.empty() || ds.empty())
    {
        Canvas::LogError(m_pDevice->GetLogger(),
            "Failed to load displaced shadow shader bytecode (VS:%s HS:%s DS:%s)",
            vs.empty() ? "MISSING" : "OK", hs.empty() ? "MISSING" : "OK",
            ds.empty() ? "MISSING" : "OK");
        Gem::ThrowGemError(Gem::Result::Fail);
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_pDisplacedShadowRootSig;
    psoDesc.VS = { vs.data(), vs.size() };
    psoDesc.HS = { hs.data(), hs.size() };
    psoDesc.DS = { ds.data(), ds.size() };
    psoDesc.PS = { nullptr, 0 };  // depth-only
    psoDesc.InputLayout = { nullptr, 0 };

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;  // matches geometry-pass winding
    // Caster-side bias for reverse-Z self-shadowing:
    //   NewDepth = OldDepth + DepthBias*r + SlopeScaledDepthBias*MaxDepthSlope
    // In reverse-Z (near=1, far=0) with a GREATER_EQUAL compare, the caster
    // must be pushed AWAY from the light (toward 0) so receivers behind it
    // (lower z) still satisfy receiver_z >= caster_z and avoid spurious
    // self-shadowing.  Both DepthBias and SlopeScaledDepthBias therefore
    // take NEGATIVE values here, the opposite of the forward-Z convention.
    psoDesc.RasterizerState.DepthBias            = -100;
    psoDesc.RasterizerState.DepthBiasClamp       = 0.0f;
    psoDesc.RasterizerState.SlopeScaledDepthBias = -2.0f;

    psoDesc.BlendState        = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;  // reverse-Z
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    psoDesc.NumRenderTargets = 0;
    psoDesc.SampleDesc.Count   = 1;
    psoDesc.SampleDesc.Quality = 0;

    ThrowFailedHResult(pD3DDevice->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_pDisplacedShadowPSO)));
    SetD3D12DebugName(m_pDisplacedShadowPSO, GetName(), "DisplacedShadowPSO");

    Canvas::LogInfo(m_pDevice->GetLogger(),
        "Displaced-mesh shadow PSO created (VS+HS+DS, depth-only, reverse-Z)");
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
    //   Slot 0: Root CBV(b0) - HlslTextConstants (screen size, offset, text color)
    //   Slot 1: Root SRV(t0) - StructuredBuffer<HlslGlyphInstance>
    //   Slot 2: Descriptor table with SRV[1] at t1 - SDFAtlas texture
    //   Static sampler at s0 - linear, clamp
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

    SetD3D12DebugName(m_pTextRootSig, GetName(), "TextRootSig");
    SetD3D12DebugName(m_pTextPSO, GetName(), "TextPSO");

    Canvas::LogInfo(m_pDevice->GetLogger(), "Text PSO created successfully");
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::EnsureRectPSO(DXGI_FORMAT rtvFormat)
{
    if (m_pRectPSO && m_RectPSOFormat == rtvFormat)
        return;

    auto shaderDir = GetShaderDirectory();
    auto vsBytecode = LoadShaderBytecode(shaderDir / "VSRect.cso");
    auto psBytecode = LoadShaderBytecode(shaderDir / "PSRect.cso");

    if (vsBytecode.empty() || psBytecode.empty())
    {
        Canvas::LogError(m_pDevice->GetLogger(), "Failed to load rect shader bytecode (VS: %s, PS: %s)",
            vsBytecode.empty() ? "MISSING" : "OK",
            psBytecode.empty() ? "MISSING" : "OK");
        Gem::ThrowGemError(Gem::Result::Fail);
    }

    ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();

    // Rect root signature:
    //   Slot 0: Root CBV(b0) - HlslRectConstants (screen size, element offset, rect size, fill color)
    // No vertex buffer - the vertex shader derives the quad from SV_VertexID + constants.
    std::vector<CD3DX12_ROOT_PARAMETER1> rectRootParams(1);
    rectRootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                                               D3D12_SHADER_VISIBILITY_VERTEX);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rectRootSigDesc(
        static_cast<UINT>(rectRootParams.size()),
        rectRootParams.data(),
        0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    CComPtr<ID3DBlob> pRSBlob;
    ThrowFailedHResult(D3D12SerializeVersionedRootSignature(&rectRootSigDesc, &pRSBlob, nullptr));
    CComPtr<ID3D12RootSignature> pRectRootSig;
    ThrowFailedHResult(pD3DDevice->CreateRootSignature(
        1, pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(), IID_PPV_ARGS(&pRectRootSig)));

    // Alpha blending: src_alpha / inv_src_alpha (same as text PSO)
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
    psoDesc.pRootSignature = pRectRootSig;
    psoDesc.VS = { vsBytecode.data(), vsBytecode.size() };
    psoDesc.PS = { psBytecode.data(), psBytecode.size() };
    psoDesc.InputLayout              = { nullptr, 0 };
    psoDesc.RasterizerState          = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState               = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.BlendState.RenderTarget[0] = rtBlend;
    psoDesc.DepthStencilState        = {};
    psoDesc.SampleMask               = UINT_MAX;
    psoDesc.PrimitiveTopologyType    = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets         = 1;
    psoDesc.RTVFormats[0]            = rtvFormat;
    psoDesc.DSVFormat                = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count         = 1;
    psoDesc.SampleDesc.Quality       = 0;

    CComPtr<ID3D12PipelineState> pRectPSO;
    ThrowFailedHResult(pD3DDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pRectPSO)));

    m_pRectRootSig.Attach(pRectRootSig.Detach());
    m_pRectPSO.Attach(pRectPSO.Detach());
    m_RectPSOFormat = rtvFormat;

    SetD3D12DebugName(m_pRectRootSig, GetName(), "RectRootSig");
    SetD3D12DebugName(m_pRectPSO, GetName(), "RectPSO");

    Canvas::LogInfo(m_pDevice->GetLogger(), "Rect PSO created successfully");
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
    //   Slot 0: Root CBV (b0) -- per-frame constants (camera, sky params, light count,
    //           tile grid dimensions).
    //   Slot 1: Descriptor table with SRV[8] at t0-t7:
    //             t0-t2 = G-buffer (normals, diffuse, world pos) [Texture2D]
    //             t3-t4 = optional skybox cubes A / B            [TextureCube]
    //             t5    = optional stars cube                    [TextureCube]
    //             t6    = optional moon billboard texture        [Texture2D]
    //             t7    = optional shadow atlas (R32_Float)      [Texture2D]
    //           Unbound slots hold null SRVs and the shader's
    //           SkyHasCubemap / HasStars / HasMoon / per-light ShadowFlags
    //           flags select active branches.  The sun has no texture
    //           (procedural disc).
    //   Slot 2: Root SRV (t8) -- StructuredBuffer<HlslLight> for the
    //           per-frame light table.  Sized to PerFrame.LightCount;
    //           always bound (a single dummy element when no lights
    //           are visible so the SRV slot is never null).
    //   Slot 3: Root SRV (t9) -- StructuredBuffer<uint> per-tile light
    //           count for Forward+ tile binning.  One uint per tile in
    //           row-major order (tile (x, y) at index y * TileCountX + x).
    //   Slot 4: Root SRV (t10) -- StructuredBuffer<uint> per-tile
    //           packed light-index lists, MAX_LIGHTS_PER_TILE uints per
    //           tile (fixed stride).  Indexed by tile-base + i, with
    //           i < TileLightCounts[tile].
    //   Static sampler s0: point/clamp  (exact G-buffer texel fetch)
    //   Static sampler s1: linear/wrap  (cube sampling for skybox + stars)
    //   Static sampler s2: linear/clamp (moon billboard quad)
    //   Static sampler s3: PCF comparison sampler (shadow atlas)
    CD3DX12_STATIC_SAMPLER_DESC staticSamplers[4] = {};
    staticSamplers[0].Init(
        0,                                      // shader register s0
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    staticSamplers[1].Init(
        1,                                      // shader register s1
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    staticSamplers[2].Init(
        2,                                      // shader register s2
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    // s3: hardware PCF (SampleCmpLevelZero).  Reverse-Z atlas (near=1,
    // far=0) means the receiver is "in shadow" when its biased depth is
    // LESS than the stored caster depth, hence GREATER_EQUAL is the
    // visibility comparison.  Border-white addressing means samples
    // outside any atlas tile return "fully lit" rather than fully
    // shadowed -- safer when receivers stray past the shadow frustum.
    staticSamplers[3].Init(
        3,                                      // shader register s3
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        0.0f, 16u,
        D3D12_COMPARISON_FUNC_GREATER_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0,  // SRV[8] at t0-t7, space0
                  D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);

    std::vector<CD3DX12_ROOT_PARAMETER1> compositeRootParams(5);
    compositeRootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                                                    D3D12_SHADER_VISIBILITY_PIXEL);
    compositeRootParams[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
    // Lights structured buffer as a root SRV at t8.  Root SRVs are
    // untyped; the shader's `StructuredBuffer<HlslLight>` declaration
    // supplies the element shape.  DATA_VOLATILE matches the upload
    // ring's per-frame lifecycle (contents change every submit).
    compositeRootParams[2].InitAsShaderResourceView(8, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                                                    D3D12_SHADER_VISIBILITY_PIXEL);
    // Per-tile light counts (t9) and packed indices (t10) for the
    // Forward+ binner.  Both rebuilt and re-uploaded each frame.
    compositeRootParams[3].InitAsShaderResourceView(9, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                                                    D3D12_SHADER_VISIBILITY_PIXEL);
    compositeRootParams[4].InitAsShaderResourceView(10, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                                                    D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC compositeRootSigDesc(
        static_cast<UINT>(compositeRootParams.size()),
        compositeRootParams.data(),
        4, staticSamplers,
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

    SetD3D12DebugName(m_pCompositeRootSig, GetName(), "CompositeRootSig");
    SetD3D12DebugName(m_pCompositePSO, GetName(), "CompositePSO");

    Canvas::LogInfo(m_pDevice->GetLogger(), "Composite PSO created successfully");
}

//================================================================================================
// Texture upload
//================================================================================================

//------------------------------------------------------------------------------------------------
Gem::Result CRenderQueue12::UploadTextureRegion(
    Canvas::XGfxSurface *pDstSurface,
    uint32_t dstX, uint32_t dstY,
    uint32_t width, uint32_t height,
    const void *pData,
    uint32_t srcRowPitch)
{
    if (!pDstSurface || !pData || width == 0 || height == 0)
        return Gem::Result::BadPointer;

    try
    {
        auto pDst = static_cast<CSurface12*>(pDstSurface);
        ID3D12Resource* pDstResource = pDst->GetD3DResource();

        constexpr uint32_t kPitchAlign = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        uint32_t alignedRowPitch = (srcRowPitch + kPitchAlign - 1) & ~(kPitchAlign - 1);
        uint64_t stagingSize = static_cast<uint64_t>(alignedRowPitch) * height;

        HostWriteAllocation hw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(stagingSize, hw));

        if (srcRowPitch == alignedRowPitch)
        {
            memcpy(hw.pMapped, pData, srcRowPitch * height);
        }
        else
        {
            for (uint32_t row = 0; row < height; ++row)
            {
                const uint8_t* pSrcRow = static_cast<const uint8_t*>(pData) + row * srcRowPitch;
                uint8_t* pDstRow = static_cast<uint8_t*>(hw.pMapped) + row * alignedRowPitch;
                memcpy(pDstRow, pSrcRow, srcRowPitch);
            }
        }

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource                          = hw.pResource;
        src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset             = hw.ResourceOffset;
        src.PlacedFootprint.Footprint.Format   = pDst->m_Desc.Format;
        src.PlacedFootprint.Footprint.Width    = width;
        src.PlacedFootprint.Footprint.Height   = height;
        src.PlacedFootprint.Footprint.Depth    = 1;
        src.PlacedFootprint.Footprint.RowPitch = alignedRowPitch;

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource        = pDstResource;
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_BOX srcBox = { 0, 0, 0, width, height, 1 };

        auto& taskGraph = m_UICommandListOpen ? m_UIGpuTaskGraph : m_GpuTaskGraph;
        auto& copyTask = taskGraph.CreateTask("UploadTextureRegion");
        taskGraph.DeclareTextureUsage(copyTask, pDst,
            D3D12_BARRIER_LAYOUT_COMMON,
            D3D12_BARRIER_SYNC_COPY,
            D3D12_BARRIER_ACCESS_COPY_DEST);
        copyTask.RecordFunc = [dst, src, srcBox, dstX, dstY](ID3D12GraphicsCommandList* pCL)
        {
            pCL->CopyTextureRegion(&dst, dstX, dstY, 0, &src, &srcBox);
        };
        taskGraph.InsertTask(copyTask);

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

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::FinalizeUploadAsShaderResource(Canvas::XGfxSurface *pSurface)
{
    if (!pSurface)
        return Gem::Result::BadPointer;

    try
    {
        auto* pDst = static_cast<CSurface12*>(pSurface);

        // No-op if the surface isn't in LAYOUT_COMMON: either it was already
        // finalized once (now SHADER_RESOURCE) or it lives in some other
        // layout that this helper isn't designed to handle.
        if (!pDst->m_CurrentLayout.m_AllSame ||
            pDst->m_CurrentLayout.m_UniformLayout != D3D12_BARRIER_LAYOUT_COMMON)
        {
            return Gem::Result::Success;
        }

        // Schedule a direct-queue barrier-only task that declares the surface
        // as SHADER_RESOURCE. The task graph's fixup-barrier mechanism will
        // emit the COMMON -> SHADER_RESOURCE transition in the fixup CL.
        // Cross-queue synchronization with the device's copy queue is handled
        // by Flush() (it issues a Wait against the copy queue fence before
        // dispatching this graph).
        if (!m_UICommandListOpen)
            EnsureTaskGraphActive();
        auto& taskGraph = m_UICommandListOpen ? m_UIGpuTaskGraph : m_GpuTaskGraph;
        auto& fixupTask = taskGraph.CreateTask("FinalizeUploadAsShaderResource");
        taskGraph.DeclareTextureUsage(fixupTask, pDst,
            D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_ALL,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        fixupTask.RecordFunc = [](ID3D12GraphicsCommandList*) { /* barrier-only */ };
        taskGraph.InsertTask(fixupTask);

        // Stamp the surface with the value this render queue will signal on
        // the next Flush(). DATA_STATIC SRV consumers (e.g. the displaced draw)
        // must wait for the fixup CL to retire on the GPU before binding the
        // descriptor: D3D12 forbids state changes on a DATA_STATIC-bound
        // resource while the binding CL is in flight, and the
        // COMMON -> SHADER_RESOURCE transition is such a change.
        pDst->m_UploadFixupToken = FenceToken{ m_TimelineId, m_FenceValue + 1 };

        return Gem::Result::Success;
    }
    catch (Gem::GemError& e) { return e.Result(); }
    catch (_com_error& e)    { return ResultFromHRESULT(e.Error()); }
}

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
        
        // CPU work: create descriptors and compute viewport/scissor
        m_GBufferRTVs[0] = CreateRenderTargetView(m_pGBufferNormals, 0, 0, 0);
        m_GBufferRTVs[1] = CreateRenderTargetView(m_pGBufferDiffuseColor, 0, 0, 0);
        m_GBufferRTVs[2] = CreateRenderTargetView(m_pGBufferWorldPos, 0, 0, 0);
        m_GBufferRTVs[3] = CreateRenderTargetView(m_pGBufferPBR, 0, 0, 0);
        m_GBufferRTVs[4] = CreateRenderTargetView(m_pGBufferEmissive, 0, 0, 0);
        m_CurrentDSV = CreateDepthStencilView(m_pDepthBuffer);
        m_CurrentRTV = CreateRenderTargetView(pBackBuffer, 0, 0, 0);

        D3D12_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        D3D12_RECT scissor = {};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<LONG>(width);
        scissor.bottom = static_cast<LONG>(height);

        // Cache so passes that temporarily switch viewport/scissor (e.g.
        // shadow atlas tile draws) can restore the back-buffer state for
        // downstream geometry + composite tasks.
        m_BackBufferViewport = viewport;
        m_BackBufferScissor  = scissor;

        // Scene task: transition G-buffers + depth, then set up geometry pass state
        auto& task = CreateGpuTask("BeginFrame_GBufferPass");
        DeclareGpuTextureUsage(task, m_pGBufferNormals,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        DeclareGpuTextureUsage(task, m_pGBufferDiffuseColor,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        DeclareGpuTextureUsage(task, m_pGBufferWorldPos,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        DeclareGpuTextureUsage(task, m_pGBufferPBR,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        DeclareGpuTextureUsage(task, m_pGBufferEmissive,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        DeclareGpuTextureUsage(task, m_pDepthBuffer,
            D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
            D3D12_BARRIER_SYNC_DEPTH_STENCIL,
            D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
        task.RecordFunc = [&, this](ID3D12GraphicsCommandList* pCL)
        {
            const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            pCL->ClearRenderTargetView(m_GBufferRTVs[0], clearColor, 0, nullptr);
            pCL->ClearRenderTargetView(m_GBufferRTVs[1], clearColor, 0, nullptr);
            pCL->ClearRenderTargetView(m_GBufferRTVs[2], clearColor, 0, nullptr);
            pCL->ClearRenderTargetView(m_GBufferRTVs[3], clearColor, 0, nullptr);
            pCL->ClearRenderTargetView(m_GBufferRTVs[4], clearColor, 0, nullptr);
            pCL->ClearDepthStencilView(m_CurrentDSV, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
            pCL->OMSetRenderTargets(5, m_GBufferRTVs, FALSE, &m_CurrentDSV);
            pCL->RSSetViewports(1, &viewport);
            pCL->RSSetScissorRects(1, &scissor);
            pCL->SetPipelineState(GetActiveDefaultPSO());
            pCL->SetGraphicsRootSignature(m_pDefaultRootSig);
            pCL->SetDescriptorHeaps(2, m_DescriptorHeapsArray);
            pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        };
        m_GpuTaskGraph.InsertTask(task);
        
        m_pCurrentSwapChain = pIntSwapChain;
        pIntSwapChain->m_BackBufferModified = true;

        // Open UI graph work CL for text/HUD recording
        m_UIGpuTaskGraph.GetWorkCommandList()->Reset(
            m_UIGpuTaskGraph.GetWorkAllocator(), nullptr);
        m_UICommandListOpen = true;

        // UI task: set up back buffer as render target for UI draws
        auto& uiBeginTask = m_UIGpuTaskGraph.CreateTask("UIBeginFrame");
        m_UIGpuTaskGraph.DeclareTextureUsage(uiBeginTask, pBackBuffer,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        uiBeginTask.RecordFunc = [&, this](ID3D12GraphicsCommandList* pCL)
        {
            pCL->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, nullptr);
            pCL->RSSetViewports(1, &viewport);
            pCL->RSSetScissorRects(1, &scissor);
            pCL->SetDescriptorHeaps(2, m_DescriptorHeapsArray);
            pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        };
        m_UIGpuTaskGraph.InsertTask(uiBeginTask);
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
Gem::Result CRenderQueue12::DrawMesh(
    Canvas::XGfxMeshData *pMeshData,
    uint32_t materialGroupIndex,
    const Canvas::Math::FloatMatrix4x4 &worldTransform,
    Canvas::XMeshInstance *pMeshInstance)
{
    try
    {
        if (!pMeshData || !m_pCurrentSwapChain)
            return Gem::Result::InvalidArg;
        
        auto pMesh = static_cast<CMeshData12*>(pMeshData);

        if (materialGroupIndex >= pMesh->GetNumMaterialGroups())
            return Gem::Result::InvalidArg;

        // PatchList4CP + displacement-equipped material -> displaced-mesh
        // draw path.  The mesh supplies CP positions (mesh-local) and UVs
        // (already mapped into the displacement map's UV space) as ordinary
        // vertex buffers; the engine binds them as StructuredBuffer SRVs
        // for the displaced VS, applies the node's world transform via the
        // per-tile CB, and routes the draw through the HS/DS/PS pipeline.
        if (pMesh->GetTopology() == Canvas::GfxPrimitiveTopology::PatchList4CP)
        {
            Canvas::XGfxMaterial *pMaterialCheck = pMesh->GetMaterial(materialGroupIndex);
            const Canvas::GfxDisplacementDesc *pDisp =
                pMaterialCheck ? pMaterialCheck->GetDisplacement() : nullptr;
            if (pDisp && pDisp->pDisplacementMap)
            {
                auto pPosEntry = pMesh->GetVertexBuffer(materialGroupIndex, Canvas::GfxVertexBufferType::Position);
                auto pUVEntry  = pMesh->GetVertexBuffer(materialGroupIndex, Canvas::GfxVertexBufferType::UV0);
                auto pNormEntry= pMesh->GetVertexBuffer(materialGroupIndex, Canvas::GfxVertexBufferType::Normal);
                if (!pPosEntry  || !pPosEntry->pBuffer  ||
                    !pUVEntry   || !pUVEntry->pBuffer   ||
                    !pNormEntry || !pNormEntry->pBuffer)
                    return Gem::Result::InvalidArg;

                DisplacedDrawDesc tdesc = {};
                memcpy(&tdesc.World, &worldTransform, sizeof(tdesc.World));
                tdesc.MapScale         = pDisp->MapScale;
                tdesc.MapBias          = pDisp->MapBias;
                tdesc.pDisplacementMap = pDisp->pDisplacementMap;
                tdesc.pAlbedo          = pMaterialCheck->GetTexture(Canvas::MaterialLayerRole::Albedo);
                tdesc.pAOMap           = pMaterialCheck->GetTexture(Canvas::MaterialLayerRole::AmbientOcclusion);
                tdesc.pRoughnessMap    = pMaterialCheck->GetTexture(Canvas::MaterialLayerRole::Roughness);
                tdesc.pPositions       = static_cast<CBuffer12*>(pPosEntry ->pBuffer.Get());
                tdesc.pUV0s            = static_cast<CBuffer12*>(pUVEntry  ->pBuffer.Get());
                tdesc.pNormals         = static_cast<CBuffer12*>(pNormEntry->pBuffer.Get());
                tdesc.CPVertexCount    = pMesh->GetTotalVertexCount();

                return SubmitDisplacedDraw(tdesc);
            }
        }

        // Fetch all per-group vertex streams. Position is required; the rest
        // are optional and gated by MATERIAL_FLAG_HAS_* bits in the per-object CB.
        auto pPosEntry      = pMesh->GetVertexBuffer(materialGroupIndex, Canvas::GfxVertexBufferType::Position);
        auto pNormEntry     = pMesh->GetVertexBuffer(materialGroupIndex, Canvas::GfxVertexBufferType::Normal);
        auto pUV0Entry      = pMesh->GetVertexBuffer(materialGroupIndex, Canvas::GfxVertexBufferType::UV0);
        auto pTanEntry      = pMesh->GetVertexBuffer(materialGroupIndex, Canvas::GfxVertexBufferType::Tangent);
        auto pBoneWEntry    = pMesh->GetVertexBuffer(materialGroupIndex, Canvas::GfxVertexBufferType::BoneWeights);
        auto pBoneIEntry    = pMesh->GetVertexBuffer(materialGroupIndex, Canvas::GfxVertexBufferType::BoneIndices);

        if (!pPosEntry || !pPosEntry->pBuffer)
            return Gem::Result::InvalidArg;

        auto pPosBuf      = static_cast<CBuffer12*>(pPosEntry->pBuffer.Get());
        CBuffer12* pNormBuf  = (pNormEntry  && pNormEntry->pBuffer)  ? static_cast<CBuffer12*>(pNormEntry->pBuffer.Get())  : nullptr;
        CBuffer12* pUV0Buf   = (pUV0Entry   && pUV0Entry->pBuffer)   ? static_cast<CBuffer12*>(pUV0Entry->pBuffer.Get())   : nullptr;
        CBuffer12* pTanBuf   = (pTanEntry   && pTanEntry->pBuffer)   ? static_cast<CBuffer12*>(pTanEntry->pBuffer.Get())   : nullptr;
        CBuffer12* pBoneWBuf = (pBoneWEntry && pBoneWEntry->pBuffer) ? static_cast<CBuffer12*>(pBoneWEntry->pBuffer.Get()) : nullptr;
        CBuffer12* pBoneIBuf = (pBoneIEntry && pBoneIEntry->pBuffer) ? static_cast<CBuffer12*>(pBoneIEntry->pBuffer.Get()) : nullptr;

        // Resolve material (may be null) and gather texture surfaces by role.
        Canvas::XGfxMaterial *pMaterial = pMesh->GetMaterial(materialGroupIndex);
        Canvas::Math::FloatVector4 baseColorFactor    = { 0.8f, 0.8f, 0.8f, 1.0f };
        Canvas::Math::FloatVector4 emissiveFactor     = { 0.0f, 0.0f, 0.0f, 0.0f };
        Canvas::Math::FloatVector4 roughMetalAOFactor = { 1.0f, 0.0f, 1.0f, 0.0f };
        CSurface12 *pTextures[6] = {}; // [Albedo, Normal, Emissive, Roughness, Metallic, AO]
        if (pMaterial)
        {
            baseColorFactor    = pMaterial->GetBaseColorFactor();
            emissiveFactor     = pMaterial->GetEmissiveFactor();
            roughMetalAOFactor = pMaterial->GetRoughMetalAOFactor();
            const Canvas::MaterialLayerRole roles[6] = {
                Canvas::MaterialLayerRole::Albedo,
                Canvas::MaterialLayerRole::Normal,
                Canvas::MaterialLayerRole::Emissive,
                Canvas::MaterialLayerRole::Roughness,
                Canvas::MaterialLayerRole::Metallic,
                Canvas::MaterialLayerRole::AmbientOcclusion,
            };
            for (UINT i = 0; i < 6; ++i)
            {
                Canvas::XGfxSurface *pSurface = pMaterial->GetTexture(roles[i]);
                if (pSurface)
                {
                    Gem::TGemPtr<CSurface12> pSurf12;
                    pSurface->QueryInterface(&pSurf12);
                    pTextures[i] = pSurf12.Get();
                }
            }
        }

        // Build MaterialFlags. Texture flags are gated by mesh capability
        // (no UV -> no albedo/normal/emissive/PBR sampling; no tangent -> no normal map).
        const bool hasUV      = (pUV0Buf  != nullptr);
        const bool hasTangent = (pTanBuf  != nullptr);
        const bool hasSkin    = (pBoneWBuf != nullptr) && (pBoneIBuf != nullptr) &&
                                 (pMeshInstance != nullptr) && (pMeshInstance->GetSkinBoneCount() > 0);
        uint32_t materialFlags = 0;
        if (hasUV)      materialFlags |= MATERIAL_FLAG_HAS_UV;
        if (hasTangent) materialFlags |= MATERIAL_FLAG_HAS_TANGENT;
        if (hasSkin)    materialFlags |= MATERIAL_FLAG_HAS_SKIN;
        if (hasUV && pTextures[0])               materialFlags |= MATERIAL_FLAG_HAS_ALBEDO_TEX;
        if (hasUV && hasTangent && pTextures[1]) materialFlags |= MATERIAL_FLAG_HAS_NORMAL_TEX;
        if (hasUV && pTextures[2])               materialFlags |= MATERIAL_FLAG_HAS_EMISSIVE_TEX;
        if (hasUV && pTextures[3])               materialFlags |= MATERIAL_FLAG_HAS_ROUGH_TEX;
        if (hasUV && pTextures[4])               materialFlags |= MATERIAL_FLAG_HAS_METAL_TEX;
        if (hasUV && pTextures[5])               materialFlags |= MATERIAL_FLAG_HAS_AO_TEX;

        // Compute and upload bone matrices when skinning is active.
        // Each matrix = InvBindPose[i] * boneNode[i].GetGlobalMatrix() - maps bind-pose
        // geometry space directly to world space for the current frame's bone pose.
        D3D12_GPU_VIRTUAL_ADDRESS boneMatricesGpuAddr = pPosBuf->GetD3DResource()->GetGPUVirtualAddress();
        if (hasSkin)
        {
            const uint32_t boneCount = pMeshInstance->GetSkinBoneCount();
            const uint64_t boneMatSize = static_cast<uint64_t>(boneCount) * sizeof(Canvas::Math::FloatMatrix4x4);
            HostWriteAllocation boneHw;
            Gem::ThrowGemError(m_UploadRing.AllocateFromRing(boneMatSize, boneHw));
            auto* pMats = static_cast<Canvas::Math::FloatMatrix4x4*>(boneHw.pMapped);
            for (uint32_t i = 0; i < boneCount; ++i)
            {
                Canvas::XSceneGraphNode*          pBone    = pMeshInstance->GetSkinBoneNode(i);
                const Canvas::Math::FloatMatrix4x4* pInvBind = pMeshInstance->GetSkinInvBindPose(i);
                // Skin bindings are fully resolved and validated at instantiation time
                // (XModel::Instantiate and CMeshInstance::SetSkinBinding reject unresolved
                // bones), so both pointers are guaranteed valid for an active skin here.
                assert(pBone && pInvBind);
                pMats[i] = *pInvBind * pBone->GetGlobalMatrix();
            }
            boneMatricesGpuAddr = boneHw.GpuAddress;
        }

        // Pack per-object constants
        HlslTypes::HlslPerObjectConstants objectConstants = {};
        static_assert(sizeof(objectConstants.World) == sizeof(worldTransform),
                      "HlslTypes::float4x4 and Math::FloatMatrix4x4 must be layout-compatible");
        memcpy(&objectConstants.World, &worldTransform, sizeof(objectConstants.World));
        memcpy(&objectConstants.WorldInvTranspose, &worldTransform, sizeof(objectConstants.WorldInvTranspose));
        objectConstants.WorldInvTranspose.m[3][0] = 0.0f;
        objectConstants.WorldInvTranspose.m[3][1] = 0.0f;
        objectConstants.WorldInvTranspose.m[3][2] = 0.0f;
        objectConstants.WorldInvTranspose.m[3][3] = 1.0f;
        memcpy(&objectConstants.BaseColorFactor,    &baseColorFactor,    sizeof(objectConstants.BaseColorFactor));
        memcpy(&objectConstants.EmissiveFactor,     &emissiveFactor,     sizeof(objectConstants.EmissiveFactor));
        memcpy(&objectConstants.RoughMetalAOFactor, &roughMetalAOFactor, sizeof(objectConstants.RoughMetalAOFactor));
        objectConstants.MaterialFlags = materialFlags;
        
        // Upload per-object constants to upload heap
        constexpr uint64_t cbAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        const uint64_t cbSize = (sizeof(HlslTypes::HlslPerObjectConstants) + cbAlignment - 1) & ~(cbAlignment - 1);
        HostWriteAllocation hw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(cbSize, hw));
        memcpy(hw.pMapped, &objectConstants, sizeof(HlslTypes::HlslPerObjectConstants));
        
        // Allocate a contiguous block of 15 descriptors:
        //   slots 0-1  : CBV[2] @ b1, b2
        //   slots 2-12 : SRV[11] @ t1..t11 (normals, UV0, tangents, albedo..AO,
        //                                    boneIndices, boneWeights)
        //   slots 13-14: UAV[2] @ u1, u2 (null)
        constexpr UINT kDescCount = 15;
        UINT baseSlot = AllocateSRVSlots(kDescCount);
        
        const UINT incSize = m_CbvSrvUavIncrement;
        ID3D12Device *pD3DDevice = m_pDevice->GetD3DDevice();
        CD3DX12_CPU_DESCRIPTOR_HANDLE baseCpuHandle(m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), baseSlot, incSize);
        CD3DX12_GPU_DESCRIPTOR_HANDLE baseGpuHandle(m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), baseSlot, incSize);
        
        // CBV b1: per-object constants
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = hw.GpuAddress;
        cbvDesc.SizeInBytes = static_cast<UINT>(cbSize);
        pD3DDevice->CreateConstantBufferView(&cbvDesc, baseCpuHandle);
        
        // CBV b2: null placeholder
        D3D12_CONSTANT_BUFFER_VIEW_DESC nullCbvDesc = {};
        pD3DDevice->CreateConstantBufferView(&nullCbvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(baseCpuHandle, 1, incSize));
        
        // Helper lambdas for null fallbacks
        D3D12_SHADER_RESOURCE_VIEW_DESC nullBufSrvDesc = {};
        nullBufSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        nullBufSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        nullBufSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        
        D3D12_SHADER_RESOURCE_VIEW_DESC nullTex2DSrvDesc = {};
        nullTex2DSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        nullTex2DSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullTex2DSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullTex2DSrvDesc.Texture2D.MipLevels = 1;
        
        auto WriteStructuredBufSRV = [&](UINT tableSlot, CBuffer12* pBuf, UINT stride)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE h(baseCpuHandle, tableSlot, incSize);
            if (pBuf)
            {
                D3D12_RESOURCE_DESC rd = pBuf->GetD3DResource()->GetDesc();
                D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
                d.Format = DXGI_FORMAT_UNKNOWN;
                d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                d.Buffer.NumElements = static_cast<UINT>(rd.Width / stride);
                d.Buffer.StructureByteStride = stride;
                pD3DDevice->CreateShaderResourceView(pBuf->GetD3DResource(), &d, h);
            }
            else
            {
                pD3DDevice->CreateShaderResourceView(nullptr, &nullBufSrvDesc, h);
            }
        };
        auto WriteTexture2DSRV = [&](UINT tableSlot, CSurface12* pSurf)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE h(baseCpuHandle, tableSlot, incSize);
            if (pSurf)
            {
                D3D12_RESOURCE_DESC rd = pSurf->GetD3DResource()->GetDesc();
                D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
                d.Format = rd.Format;
                d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                d.Texture2D.MipLevels = rd.MipLevels ? rd.MipLevels : 1;
                pD3DDevice->CreateShaderResourceView(pSurf->GetD3DResource(), &d, h);
            }
            else
            {
                pD3DDevice->CreateShaderResourceView(nullptr, &nullTex2DSrvDesc, h);
            }
        };
        
        // SRV t1: normals (slot 2)
        WriteStructuredBufSRV(2, pNormBuf, sizeof(Canvas::Math::FloatVector4));
        // SRV t2: UV0 (slot 3)
        WriteStructuredBufSRV(3, pUV0Buf, sizeof(Canvas::Math::FloatVector2));
        // SRV t3: tangents (slot 4)
        WriteStructuredBufSRV(4, pTanBuf, sizeof(Canvas::Math::FloatVector4));
        // SRV t4..t9: material textures (slots 5..10)
        WriteTexture2DSRV(5,  pTextures[0]); // Albedo
        WriteTexture2DSRV(6,  pTextures[1]); // Normal map
        WriteTexture2DSRV(7,  pTextures[2]); // Emissive
        WriteTexture2DSRV(8,  pTextures[3]); // Roughness
        WriteTexture2DSRV(9,  pTextures[4]); // Metallic
        WriteTexture2DSRV(10, pTextures[5]); // AO
        // SRV t10: bone indices (slot 11); SRV t11: bone weights (slot 12)
        WriteStructuredBufSRV(11, pBoneIBuf, sizeof(Canvas::Math::UIntVector4));
        WriteStructuredBufSRV(12, pBoneWBuf, sizeof(Canvas::Math::FloatVector4));

        // UAVs u1, u2: null (slots 13, 14)
        D3D12_UNORDERED_ACCESS_VIEW_DESC nullUavDesc = {};
        nullUavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        nullUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        for (UINT i = 13; i <= 14; ++i)
            pD3DDevice->CreateUnorderedAccessView(nullptr, nullptr, &nullUavDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(baseCpuHandle, i, incSize));
        
        // Set root SRV (param 1) for positions (t0)
        D3D12_GPU_VIRTUAL_ADDRESS posGpuAddr = pPosBuf->GetD3DResource()->GetGPUVirtualAddress();
        D3D12_RESOURCE_DESC posDesc = pPosBuf->GetD3DResource()->GetDesc();
        UINT vertexCount = static_cast<UINT>(posDesc.Width / sizeof(Canvas::Math::FloatVector4));
        
        // Create a task for the draw call
        auto& drawTask = CreateGpuTask("DrawMesh");
        m_GpuTaskGraph.DeclareBufferUsage(drawTask, pPosBuf,
            D3D12_BARRIER_SYNC_VERTEX_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        if (pNormBuf)
            m_GpuTaskGraph.DeclareBufferUsage(drawTask, pNormBuf,
                D3D12_BARRIER_SYNC_VERTEX_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        if (pUV0Buf)
            m_GpuTaskGraph.DeclareBufferUsage(drawTask, pUV0Buf,
                D3D12_BARRIER_SYNC_VERTEX_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        if (pTanBuf)
            m_GpuTaskGraph.DeclareBufferUsage(drawTask, pTanBuf,
                D3D12_BARRIER_SYNC_VERTEX_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        if (pBoneIBuf)
            m_GpuTaskGraph.DeclareBufferUsage(drawTask, pBoneIBuf,
                D3D12_BARRIER_SYNC_VERTEX_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        if (pBoneWBuf)
            m_GpuTaskGraph.DeclareBufferUsage(drawTask, pBoneWBuf,
                D3D12_BARRIER_SYNC_VERTEX_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        for (UINT i = 0; i < 6; ++i)
        {
            if (pTextures[i])
            {
                DeclareGpuTextureUsage(drawTask, pTextures[i],
                    D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                    D3D12_BARRIER_SYNC_PIXEL_SHADING,
                    D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            }
        }
        drawTask.RecordFunc = [posGpuAddr, boneMatricesGpuAddr, baseGpuHandle, vertexCount, this](ID3D12GraphicsCommandList* pCL)
        {
            pCL->SetPipelineState(GetActiveDefaultPSO());
            pCL->SetGraphicsRootSignature(m_pDefaultRootSig);
            pCL->OMSetRenderTargets(5, m_GBufferRTVs, FALSE, &m_CurrentDSV);
            pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            pCL->SetDescriptorHeaps(2, m_DescriptorHeapsArray);
            pCL->SetGraphicsRootShaderResourceView(1, posGpuAddr);
            pCL->SetGraphicsRootDescriptorTable(3, baseGpuHandle);
            pCL->SetGraphicsRootShaderResourceView(4, boneMatricesGpuAddr);
            pCL->DrawInstanced(vertexCount, 1, 0, 0);
        };
        m_GpuTaskGraph.InsertTask(drawTask);
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
Gem::Result CRenderQueue12::DrawUIText(
    const Canvas::GfxResourceAllocation& glyphSRV,
    Canvas::XGfxSurface* pGlyphAtlas,
    const Canvas::Math::FloatVector4& textColor,
    const Canvas::Math::FloatVector2& elementOffset)
{
    if (!pGlyphAtlas || !m_pCurrentSwapChain || !glyphSRV.pBuffer)
        return Gem::Result::InvalidArg;

    try
    {
        DXGI_FORMAT rtvFormat = m_pCurrentSwapChain->m_pSurface->GetD3DResource()->GetDesc().Format;
        EnsureTextPSO(rtvFormat);

        auto pAtlas = static_cast<CSurface12*>(pGlyphAtlas);
        CSurface12* pBackBuffer = m_pCurrentSwapChain->m_pSurface;
        auto pGlyphBuf = static_cast<CBuffer12*>(glyphSRV.pBuffer.Get());

        assert(glyphSRV.Size % sizeof(HlslTypes::HlslGlyphInstance) == 0);
        uint32_t glyphCount = static_cast<uint32_t>(glyphSRV.Size / sizeof(HlslTypes::HlslGlyphInstance));
        uint32_t vertexCount = glyphCount * 6;

        // Allocate all CPU-side resources before creating the GPU task
        constexpr uint64_t kCBVSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        HostWriteAllocation hw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(kCBVSize, hw));

        HlslTypes::HlslTextConstants tc = {};
        tc.ScreenSize = { static_cast<float>(m_DepthBufferWidth), static_cast<float>(m_DepthBufferHeight) };
        tc.ElementOffset = { elementOffset.X, elementOffset.Y };
        tc.TextColor = { textColor.X, textColor.Y, textColor.Z, textColor.W };
        memcpy(hw.pMapped, &tc, sizeof(tc));

        ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();
        const UINT incSize = m_CbvSrvUavIncrement;
        UINT srvSlot = AllocateSRVSlots(1);

        CD3DX12_CPU_DESCRIPTOR_HANDLE srvCpuHandle(m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), srvSlot, incSize);
        CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), srvSlot, incSize);
        pD3DDevice->CreateShaderResourceView(pAtlas->GetD3DResource(), nullptr, srvCpuHandle);

        D3D12_GPU_VIRTUAL_ADDRESS cbvAddr = hw.GpuAddress;
        D3D12_GPU_VIRTUAL_ADDRESS glyphAddr = pGlyphBuf->GetD3DResource()->GetGPUVirtualAddress() + glyphSRV.Offset;

        // All resources ready - now create the GPU task
        auto& drawTask = m_UIGpuTaskGraph.CreateTask("DrawUIText");
        m_UIGpuTaskGraph.DeclareTextureUsage(drawTask, pAtlas,
            D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_PIXEL_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        m_UIGpuTaskGraph.DeclareTextureUsage(drawTask, pBackBuffer,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        m_UIGpuTaskGraph.DeclareBufferUsage(drawTask, pGlyphBuf,
            D3D12_BARRIER_SYNC_VERTEX_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

        drawTask.RecordFunc = [cbvAddr, glyphAddr, srvGpuHandle, vertexCount, this](ID3D12GraphicsCommandList* pCL)
        {
            pCL->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, nullptr);
            pCL->SetDescriptorHeaps(2, m_DescriptorHeapsArray);
            pCL->SetGraphicsRootSignature(m_pTextRootSig);
            pCL->SetPipelineState(m_pTextPSO);
            pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            pCL->SetGraphicsRootConstantBufferView(0, cbvAddr);
            pCL->SetGraphicsRootDescriptorTable(2, srvGpuHandle);
            pCL->SetGraphicsRootShaderResourceView(1, glyphAddr);
            pCL->DrawInstanced(vertexCount, 1, 0, 0);
        };
        m_UIGpuTaskGraph.InsertTask(drawTask);
    }
    catch (Gem::GemError& e)
    {
        return e.Result();
    }
    catch (_com_error& e)
    {
        return ResultFromHRESULT(e.Error());
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::FlushPendingGlyphUploads()
{
    auto& cache = m_pDevice->GetGlyphCache();
    if (!cache.HasPendingUploads())
        return;

    auto* pAtlas = m_pDevice->GetGlyphAtlasSurface();
    if (!pAtlas)
        return;

    const uint8_t* pStagingData = cache.GetStagingData();
    auto uploads = cache.TakePendingUploads();
    for (auto& upload : uploads)
    {
        Gem::ThrowGemError(UploadTextureRegion(
            pAtlas, upload.AtlasX, upload.AtlasY,
            upload.Width, upload.Height,
            pStagingData + upload.PixelOffset,
            upload.Width * upload.BytesPerPixel));
    }
    cache.ClearStagingBuffer();
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::DeferRelease(Gem::XGeneric* pResource)
{
    if (!pResource)
        return;

    Gem::TGemPtr<Gem::XGeneric> ref;
    pResource->QueryInterface(&ref);
    if (!ref)
        return;

    // Stamp with m_FenceValue + 1: the value that the NEXT Flush will signal.
    // Resources passed to DeferRelease are by definition still in use by commands
    // currently being recorded, so they must outlive the upcoming submission.
    m_pDevice->GetResourceManager().DeferRelease(
        std::move(ref),
        FenceToken{ m_TimelineId, m_FenceValue + 1 });
}

//------------------------------------------------------------------------------------------------
Gem::Result CRenderQueue12::DrawUIRect(
    const Canvas::Math::FloatVector2& rectSize,
    const Canvas::Math::FloatVector4& fillColor,
    const Canvas::Math::FloatVector2& elementOffset)
{
    if (!m_pCurrentSwapChain)
        return Gem::Result::InvalidArg;

    try
    {
        DXGI_FORMAT rtvFormat = m_pCurrentSwapChain->m_pSurface->GetD3DResource()->GetDesc().Format;
        EnsureRectPSO(rtvFormat);

        CSurface12* pBackBuffer = m_pCurrentSwapChain->m_pSurface;

        // Allocate all CPU-side resources before creating the GPU task
        constexpr uint64_t kCBVSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        HostWriteAllocation hw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(kCBVSize, hw));

        HlslTypes::HlslRectConstants rc = {};
        rc.ScreenSize = { static_cast<float>(m_DepthBufferWidth), static_cast<float>(m_DepthBufferHeight) };
        rc.ElementOffset = { elementOffset.X, elementOffset.Y };
        rc.RectSize = { rectSize.X, rectSize.Y };
        rc.FillColor = { fillColor.X, fillColor.Y, fillColor.Z, fillColor.W };
        memcpy(hw.pMapped, &rc, sizeof(rc));

        D3D12_GPU_VIRTUAL_ADDRESS cbvAddr = hw.GpuAddress;

        // All resources ready - now create the GPU task
        auto& drawTask = m_UIGpuTaskGraph.CreateTask("DrawUIRect");
        m_UIGpuTaskGraph.DeclareTextureUsage(drawTask, pBackBuffer,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);

        drawTask.RecordFunc = [cbvAddr, this](ID3D12GraphicsCommandList* pCL)
        {
            pCL->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, nullptr);
            pCL->SetGraphicsRootSignature(m_pRectRootSig);
            pCL->SetPipelineState(m_pRectPSO);
            pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            pCL->SetGraphicsRootConstantBufferView(0, cbvAddr);
            pCL->DrawInstanced(6, 1, 0, 0);
        };
        m_UIGpuTaskGraph.InsertTask(drawTask);
    }
    catch (Gem::GemError& e)
    {
        return e.Result();
    }
    catch (_com_error& e)
    {
        return ResultFromHRESULT(e.Error());
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::SubmitForRender(Canvas::XSceneGraphNode *pNode)
{
    if (!pNode)
        return Gem::Result::InvalidArg;

    // Route lights directly to SubmitLight -- they don't go in the renderable
    // queue.  At the same time, accumulate every spatial element's world AABB
    // into the per-frame scene bounds (consumed by the shadow pass).
    const UINT elementCount = pNode->GetBoundElementCount();
    if (elementCount > 0)
    {
        const Canvas::Math::FloatMatrix4x4 nodeWorld = pNode->GetGlobalMatrix();
        for (UINT i = 0; i < elementCount; ++i)
        {
            Canvas::XSceneGraphElement* pElement = pNode->GetBoundElement(i);

            Gem::TGemPtr<Canvas::XLight> pLight;
            if (SUCCEEDED(pElement->QueryInterface(&pLight)))
                Gem::ThrowGemError(SubmitLight(pLight));

            const Canvas::Math::AABB localBounds = pElement->GetLocalBounds();
            if (!localBounds.IsEmpty())
                m_FrameWorldBounds.ExpandToInclude(localBounds.Transform(nodeWorld));
        }
    }

    m_RenderableQueue.push_back(pNode);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::SubmitForUIRender(Canvas::XUIGraphNode *pNode)
{
    if (!pNode)
        return Gem::Result::InvalidArg;

    m_UIRenderableQueue.push_back(pNode);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CRenderQueue12::SetActiveCamera(Canvas::XCamera *pCamera)
{
    m_pActiveCamera = pCamera;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CRenderQueue12::SetVisibleLights(Canvas::XLight* const* ppLights, size_t count)
{
    // The filter is ALWAYS installed by this call -- including when
    // count == 0, which expresses "no light is visible this frame,
    // drop every SubmitLight."  Callers that never invoke
    // SetVisibleLights leave the filter inactive (the default), in
    // which case SubmitLight accepts every enabled light in
    // submission order up to the cap.  EndFrame restores the
    // inactive state so each frame starts clean.
    m_VisibleLightFilter.clear();
    m_HasVisibleLightFilter = true;

    if (ppLights == nullptr || count == 0)
        return;

    m_VisibleLightFilter.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        if (ppLights[i])
            m_VisibleLightFilter.insert(ppLights[i]);
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CRenderQueue12::SetBackground(const Canvas::GfxBackgroundDesc *pDesc)
{
    if (pDesc)
    {
        m_Background    = *pDesc;
        m_pSkyCubeA     = pDesc->pSkyboxCubemapA;
        m_pSkyCubeB     = pDesc->pSkyboxCubemapB;
        m_pStarsCube    = pDesc->pStarsCubemap;
        m_pMoonTexture  = pDesc->pMoonTexture;
    }
    else
    {
        m_Background    = {};
        m_pSkyCubeA     = nullptr;
        m_pSkyCubeB     = nullptr;
        m_pStarsCube    = nullptr;
        m_pMoonTexture  = nullptr;
    }
}

//------------------------------------------------------------------------------------------------
Gem::Result CRenderQueue12::SubmitDisplacedDraw(const DisplacedDrawDesc &desc)
{
    if (!desc.pDisplacementMap || !desc.pAlbedo || !desc.pAOMap || !desc.pRoughnessMap)
        return Gem::Result::InvalidArg;
    if (!desc.pPositions || !desc.pUV0s || !desc.pNormals)
        return Gem::Result::InvalidArg;
    if (desc.CPVertexCount == 0 || (desc.CPVertexCount % 4u) != 0)
        return Gem::Result::InvalidArg;

    m_DisplacedDraws.push_back(desc);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Canvas::Math::FloatMatrix4x4 CRenderQueue12::BuildDirectionalShadowMatrixFromBounds(
    const Canvas::Math::FloatVector4& lightDir,
    const Canvas::Math::FloatVector4& worldUp,
    const Canvas::Math::AABB&         sceneBounds,
    UINT                              resolution)
{
    using namespace Canvas::Math;

    if (sceneBounds.IsEmpty())
        return FloatMatrix4x4::Identity();

    // Build the same LHS light-view basis as BuildDirectionalShadowMatrix
    // so caster and receiver matrices agree.
    FloatVector4 viewFwd = lightDir;
    {
        const float lenSq = DotProduct(viewFwd, viewFwd);
        viewFwd = (lenSq > 1e-12f) ? viewFwd * (1.0f / std::sqrt(lenSq))
                                   : FloatVector4(1.0f, 0.0f, 0.0f, 0.0f);
    }
    FloatVector4 up = worldUp;
    if (std::fabs(DotProduct(up, viewFwd)) > 0.999f)
        up = FloatVector4(1.0f, 0.0f, 0.0f, 0.0f);
    FloatVector4 viewUp = up - viewFwd * DotProduct(up, viewFwd);
    {
        const float lenSq = DotProduct(viewUp, viewUp);
        viewUp = (lenSq > 1e-12f) ? viewUp * (1.0f / std::sqrt(lenSq))
                                  : FloatVector4(0.0f, 0.0f, 1.0f, 0.0f);
    }
    FloatVector4 viewRight = CrossProduct(viewUp, viewFwd);

    // Project the 8 corners of the world-space scene AABB into light-view
    // space and AABB the result; that gives the tightest ortho box that
    // still encloses every scene point in the light's frame.
    AABB lightViewBounds;
    for (int i = 0; i < 8; ++i)
    {
        const FloatVector4 corner(
            (i & 1) ? sceneBounds.Max.X : sceneBounds.Min.X,
            (i & 2) ? sceneBounds.Max.Y : sceneBounds.Min.Y,
            (i & 4) ? sceneBounds.Max.Z : sceneBounds.Min.Z,
            1.0f);
        lightViewBounds.ExpandToInclude(FloatVector4(
            DotProduct(corner, viewRight),
            DotProduct(corner, viewUp),
            DotProduct(corner, viewFwd),
            0.0f));
    }

    // Symmetric square ortho about the scene's light-view centre.  The
    // half-width covers the wider of the two lateral extents so the box
    // is square (matches the shadow atlas tile's square aspect).
    const FloatVector4 lvCenter  = lightViewBounds.GetCenter();
    const FloatVector4 lvExtents = lightViewBounds.GetExtents();
    const float halfWidth = std::max(lvExtents.X, lvExtents.Y);
    const float depthRange = (std::max)(2.0f * lvExtents.Z, 1.0f);

    // Texel-snap the centre in light-view XY to suppress shimmer.
    const UINT  res          = (resolution > 0u) ? resolution : 1u;
    const float texelMeters  = (2.0f * halfWidth) / static_cast<float>(res);
    const float snappedX     = std::floor(lvCenter.X / texelMeters) * texelMeters;
    const float snappedY     = std::floor(lvCenter.Y / texelMeters) * texelMeters;

    // Eye sits half a depthRange behind the scene centre along -viewFwd
    // so [zNear, zFar] = [0, depthRange] straddles the scene depth slab.
    const float zNear = 0.0f;
    const float zFar  = depthRange;
    const float eyeZ  = lvCenter.Z - 0.5f * zFar;

    FloatMatrix4x4 view = {};
    view[0][0] = viewRight.X; view[0][1] = viewUp.X; view[0][2] = viewFwd.X; view[0][3] = 0.0f;
    view[1][0] = viewRight.Y; view[1][1] = viewUp.Y; view[1][2] = viewFwd.Y; view[1][3] = 0.0f;
    view[2][0] = viewRight.Z; view[2][1] = viewUp.Z; view[2][2] = viewFwd.Z; view[2][3] = 0.0f;
    view[3][0] = -snappedX;
    view[3][1] = -snappedY;
    view[3][2] = -eyeZ;
    view[3][3] = 1.0f;

    FloatMatrix4x4 proj = OrthoReverseZ(halfWidth, halfWidth, zNear, zFar);
    return view * proj;
}

//------------------------------------------------------------------------------------------------
Gem::Result CRenderQueue12::SubmitLight(Canvas::XLight *pLight)
{
    if (!pLight)
        return Gem::Result::Success;

    // Skip disabled lights
    if (!(pLight->GetFlags() & Canvas::LightFlags::Enabled))
        return Gem::Result::Success;

    // Per-frame visibility filter (installed by Scene from the
    // LightBVH frustum cull).  When active, only lights present in
    // the allowlist consume a slot; the rest are silently dropped.
    // Inactive filter -> every enabled light passes through in
    // submission order.
    if (m_HasVisibleLightFilter && m_VisibleLightFilter.find(pLight) == m_VisibleLightFilter.end())
        return Gem::Result::Success;

    // Reserve to a reasonable capacity on first push so typical scenes
    // hit at most one allocation per process lifetime.
    if (m_Lights.empty())
        m_Lights.reserve(64);

    m_Lights.emplace_back();
    HlslTypes::HlslLight& gpu = m_Lights.back();
    ++m_LightCount;
    gpu = {};
    gpu.Type = static_cast<uint32_t>(pLight->GetType());

    float attenConstant = 1.0f;
    float attenLinear = 0.0f;
    float attenQuadratic = 0.0f;
    pLight->GetAttenuation(&attenConstant, &attenLinear, &attenQuadratic);

    float range = pLight->GetRange();
    if (range < 0.0f)
        range = 0.0f;

    // Preserve imported attenuation coefficients exactly for physically faithful falloff.
    // Only guard against an invalid all-zero denominator.
    if (attenConstant == 0.0f && attenLinear == 0.0f && attenQuadratic == 0.0f)
        attenConstant = 1.0f;

    gpu.AttenuationAndRange = { attenConstant, attenLinear, attenQuadratic, range };

    // Pre-multiply color by intensity
    auto color = pLight->GetColor();
    float intensity = pLight->GetIntensity();
    gpu.Color = { color[0] * intensity, color[1] * intensity, color[2] * intensity, 0.0f };

    // Extract direction/position from the attached scene graph node
    auto *pNode = pLight->GetAttachedNode();
    if (pNode)
    {
        auto world = pNode->GetGlobalMatrix();
        if (pLight->GetType() == Canvas::LightType::Directional)
        {
            Canvas::Math::FloatVector4 dir(world[0][0], world[0][1], world[0][2], 0.0f);
            dir = dir.Normalize();
            gpu.DirectionOrPosition = { dir[0], dir[1], dir[2], 0.0f };
        }
        else if (pLight->GetType() == Canvas::LightType::Spot)
        {
            // Spot lights need both the world-space apex (consumed as
            // DirectionOrPosition by the shader's `toLight = pos - P`
            // computation) and a unit forward axis for the cone test.
            // The axis goes into DirectionAndSpot below; the position
            // belongs here.
            gpu.DirectionOrPosition = { world[3][0], world[3][1], world[3][2], 1.0f };

            Canvas::Math::FloatVector4 dir(world[0][0], world[0][1], world[0][2], 0.0f);
            dir = dir.Normalize();

            float innerAngle = 0.785398f;
            float outerAngle = 1.047198f;
            pLight->GetSpotAngles(&innerAngle, &outerAngle);
            if (innerAngle > outerAngle)
                std::swap(innerAngle, outerAngle);

            gpu.DirectionAndSpot = {
                dir[0],
                dir[1],
                dir[2],
                std::cos(outerAngle * 0.5f)
            };
            gpu.Color.w = std::cos(innerAngle * 0.5f);
        }
        else
        {
            gpu.DirectionOrPosition = { world[3][0], world[3][1], world[3][2], 1.0f };
        }
    }
    else
    {
        if (pLight->GetType() == Canvas::LightType::Directional)
            gpu.DirectionOrPosition = { 0.0f, 1.0f, 0.0f, 0.0f };
        else if (pLight->GetType() == Canvas::LightType::Spot)
            gpu.DirectionOrPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
    }

    if (pLight->GetType() == Canvas::LightType::Directional)
    {
        gpu.DirectionAndSpot = { gpu.DirectionOrPosition.x, gpu.DirectionOrPosition.y, gpu.DirectionOrPosition.z, -1.0f };
        gpu.AttenuationAndRange = { 1.0f, 0.0f, 0.0f, 0.0f };
    }
    else if (pLight->GetType() == Canvas::LightType::Point)
    {
        gpu.DirectionAndSpot = { 0.0f, 0.0f, 0.0f, -1.0f };
    }
    else if (pLight->GetType() == Canvas::LightType::Ambient)
    {
        gpu.DirectionAndSpot = { 0.0f, 0.0f, 0.0f, -1.0f };
        gpu.AttenuationAndRange = { 1.0f, 0.0f, 0.0f, 0.0f };
    }

    // Precompute cutoff distance on CPU for point/spot lights so the shader can
    // skip the per-pixel quadratic solve.  Stored in AttenuationAndRange.w,
    // replacing the raw authored range (which was only used to clamp this value).
    if (pLight->GetType() == Canvas::LightType::Point || pLight->GetType() == Canvas::LightType::Spot)
    {
        float peakIntensity = (std::max)(gpu.Color.x, (std::max)(gpu.Color.y, gpu.Color.z));
        gpu.AttenuationAndRange.w = ComputeLightCutoffDistance(
            peakIntensity, attenConstant, attenLinear, attenQuadratic,
            range, kDefaultLightCullThreshold);
    }

    // Shadow casting: directional lights with LightFlags::CastsShadows
    // record an intent here; the actual atlas allocation and shadow-
    // matrix derivation happens in ResolveShadowCasters at EndFrame,
    // after scene traversal has completed and m_FrameWorldBounds is
    // final.  Deferring is necessary because lights and meshes can be
    // visited in any order during scene traversal -- computing the
    // shadow region here would use a partial bounds aggregate.
    if ((pLight->GetFlags() & Canvas::LightFlags::CastsShadows) &&
        pLight->GetType() == Canvas::LightType::Directional &&
        pNode != nullptr)
    {
        PendingShadowCaster caster = {};
        caster.LightSlotIndex = m_LightCount - 1;  // we incremented above
        caster.pLight         = pLight;
        caster.Resolved       = false;
        m_PendingShadowCasters.push_back(caster);
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::BuildTileLightLists(uint32_t framebufferWidth,
                                         uint32_t framebufferHeight,
                                         const Canvas::Math::FloatMatrix4x4& viewProj)
{
    using Canvas::Math::AABB;
    using Canvas::Math::Frustum;
    using Canvas::Math::FloatVector4;
    using Canvas::Math::FloatMatrix4x4;

    if (framebufferWidth == 0 || framebufferHeight == 0)
    {
        m_LightTileCountX = 0;
        m_LightTileCountY = 0;
        m_TileLightCounts.clear();
        m_TileLightIndices.clear();
        return;
    }

    constexpr uint32_t kTileSize = LIGHT_TILE_SIZE_PIXELS;
    constexpr uint32_t kPerTile  = MAX_LIGHTS_PER_TILE;
    m_LightTileCountX = (framebufferWidth  + kTileSize - 1) / kTileSize;
    m_LightTileCountY = (framebufferHeight + kTileSize - 1) / kTileSize;
    const uint32_t totalTiles = m_LightTileCountX * m_LightTileCountY;

    m_TileLightCounts.assign(totalTiles, 0);
    m_TileLightIndices.assign(static_cast<size_t>(totalTiles) * kPerTile, 0);

    if (m_LightCount == 0)
        return;

    // Split the per-frame light table into:
    //   alwaysIndices   -- ambient / directional / area (no spatial
    //                      bound; copied into every tile),
    //   spatialIndices  -- point / spot (gated by per-tile frustum).
    // Influence AABBs for spatial lights are derived from HlslLight
    // fields (DirectionOrPosition.xyz as the apex/center,
    // AttenuationAndRange.w as the cutoff distance, which is already
    // the precomputed range CPU-side).  For spot lights this uses the
    // bounding sphere of the cone -- conservative at the tile tier,
    // tightened later if profiling shows it matters.
    std::vector<uint32_t> alwaysIndices;
    std::vector<uint32_t> spatialIndices;
    std::vector<AABB>     spatialBoxes;
    alwaysIndices.reserve(m_LightCount);
    spatialIndices.reserve(m_LightCount);
    spatialBoxes.reserve(m_LightCount);

    for (uint32_t i = 0; i < m_LightCount; ++i)
    {
        const HlslTypes::HlslLight& L = m_Lights[i];
        const bool spatial =
            (L.Type == LIGHT_POINT) || (L.Type == LIGHT_SPOT);

        if (!spatial)
        {
            alwaysIndices.push_back(i);
            continue;
        }

        const float r = L.AttenuationAndRange.w;
        if (r <= 0.0f)
            continue; // zero-cutoff light -- contributes nothing anywhere

        const float cx = L.DirectionOrPosition.x;
        const float cy = L.DirectionOrPosition.y;
        const float cz = L.DirectionOrPosition.z;
        spatialIndices.push_back(i);
        spatialBoxes.emplace_back(
            FloatVector4(cx - r, cy - r, cz - r, 0.0f),
            FloatVector4(cx + r, cy + r, cz + r, 0.0f));
    }

    // Seed every tile with the always-on lights (capped at kPerTile;
    // in practice a scene has at most a handful, so the cap is never
    // a real constraint here -- the loop just defends against
    // pathological inputs).
    const uint32_t alwaysToCopy = std::min<uint32_t>(
        static_cast<uint32_t>(alwaysIndices.size()), kPerTile);
    for (uint32_t t = 0; t < totalTiles; ++t)
    {
        uint32_t* dst = &m_TileLightIndices[static_cast<size_t>(t) * kPerTile];
        for (uint32_t a = 0; a < alwaysToCopy; ++a)
            dst[a] = alwaysIndices[a];
        m_TileLightCounts[t] = alwaysToCopy;
    }

    if (spatialIndices.empty())
        return;

    // Per-tile binning.  Build a sub-frustum from the camera's view-
    // projection scaled to each tile's NDC sub-rect and test each
    // spatial light's influence AABB against it.  The sub-rect remap
    // matrix R rescales clip-space xy from [xMin,xMax] x [yMin,yMax]
    // to [-1,+1]^2 so the existing Frustum::FromViewProjection plane
    // extraction works unchanged.  Reverse-Z near/far are inherited
    // from the camera projection.
    const float fbW = static_cast<float>(framebufferWidth);
    const float fbH = static_cast<float>(framebufferHeight);

    for (uint32_t ty = 0; ty < m_LightTileCountY; ++ty)
    {
        for (uint32_t tx = 0; tx < m_LightTileCountX; ++tx)
        {
            const float xPxMin = static_cast<float>(tx * kTileSize);
            const float xPxMax = std::min<float>(static_cast<float>((tx + 1) * kTileSize), fbW);
            const float yPxMin = static_cast<float>(ty * kTileSize);
            const float yPxMax = std::min<float>(static_cast<float>((ty + 1) * kTileSize), fbH);

            // Screen-pixel origin is top-left; NDC y is bottom-up, so
            // flip the y mapping.
            const float xNdcMin = (xPxMin / fbW) * 2.0f - 1.0f;
            const float xNdcMax = (xPxMax / fbW) * 2.0f - 1.0f;
            const float yNdcMax = 1.0f - (yPxMin / fbH) * 2.0f;
            const float yNdcMin = 1.0f - (yPxMax / fbH) * 2.0f;

            const float cx = (xNdcMin + xNdcMax) * 0.5f;
            const float cy = (yNdcMin + yNdcMax) * 0.5f;
            const float sx = (xNdcMax - xNdcMin);
            const float sy = (yNdcMax - yNdcMin);
            // Guard against zero-size tiles at the framebuffer edge.
            if (sx <= 0.0f || sy <= 0.0f)
                continue;

            FloatMatrix4x4 R = Canvas::Math::IdentityMatrix<float, 4, 4>();
            R[0][0] =  2.0f / sx;
            R[1][1] =  2.0f / sy;
            R[3][0] = -2.0f * cx / sx;
            R[3][1] = -2.0f * cy / sy;

            const FloatMatrix4x4 tileVP = viewProj * R;
            const Frustum tileFrustum = Frustum::FromViewProjection(tileVP, /*reverseZ*/ true);

            const uint32_t tileIdx = ty * m_LightTileCountX + tx;
            uint32_t*      dst     = &m_TileLightIndices[static_cast<size_t>(tileIdx) * kPerTile];
            uint32_t       cnt     = m_TileLightCounts[tileIdx];

            for (size_t si = 0; si < spatialIndices.size(); ++si)
            {
                if (cnt >= kPerTile)
                    break;
                if (tileFrustum.IntersectsAABB(spatialBoxes[si]))
                    dst[cnt++] = spatialIndices[si];
            }
            m_TileLightCounts[tileIdx] = cnt;
        }
    }
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::ResolveShadowCasters()
{
    // No geometry bounds -> no shadow region this frame.  Casters
    // stay unresolved and produce no shadow output; the corresponding
    // HlslLight.ShadowFlags is left at 0 so the composite skips PCF.
    if (m_FrameWorldBounds.IsEmpty() || m_PendingShadowCasters.empty())
        return;

    EnsureShadowAtlas();

    const Canvas::Math::FloatVector4 worldUp(0.0f, 0.0f, 1.0f, 0.0f);

    for (auto& caster : m_PendingShadowCasters)
    {
        Canvas::XLight* pLight = caster.pLight;
        if (!pLight)
            continue;

        UINT resolution = pLight->GetShadowResolution();
        if (resolution == 0u)
            resolution = kShadowAtlasDefaultSize / kShadowAtlasTilesPerSide;

        ShadowAtlasTile tile = AllocateShadowTile(resolution);
        if (!tile.Valid)
            continue;   // atlas full this frame; drop this caster

        HlslTypes::HlslLight& gpu = m_Lights[caster.LightSlotIndex];

        const Canvas::Math::FloatVector4 lightDir(
            gpu.DirectionOrPosition.x,
            gpu.DirectionOrPosition.y,
            gpu.DirectionOrPosition.z,
            0.0f);

        const Canvas::Math::FloatMatrix4x4 shadowViewProj =
            BuildDirectionalShadowMatrixFromBounds(
                lightDir, worldUp, m_FrameWorldBounds, tile.PixelSize);

        float constantBias = 0.0f, slopeScaleBias = 0.0f, normalOffset = 0.0f;
        pLight->GetShadowDepthBias(&constantBias, &slopeScaleBias, &normalOffset);

        gpu.ShadowFlags              = SHADOW_FLAG_HAS_SHADOW;
        gpu.ShadowDepthBias          = constantBias;
        gpu.ShadowNormalOffsetTexels = normalOffset;
        gpu.ShadowAtlasRectUV        = {
            tile.AtlasRectUV[0], tile.AtlasRectUV[1],
            tile.AtlasRectUV[2], tile.AtlasRectUV[3] };
        // HlslLight.ShadowViewProj is ROW_MAJOR; CPU row[i][j] maps
        // element-for-element to the GPU layout.
        memcpy(&gpu.ShadowViewProj, &shadowViewProj, sizeof(gpu.ShadowViewProj));

        caster.Resolved       = true;
        caster.ShadowViewProj = shadowViewProj;
        caster.TilePixelX     = tile.PixelX;
        caster.TilePixelY     = tile.PixelY;
        caster.TilePixelSize  = tile.PixelSize;
    }
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::EndFrame()
{
    try
    {
        // Resolve shadow-caster intents recorded during scene submission.
        // This must run before the per-frame CB is built so each light's
        // HlslLight entry carries its final ShadowFlags / atlas tile / view-
        // proj.
        ResolveShadowCasters();

        // Build per-frame constants from the active camera + accumulated lights
        // (lights were accumulated during SubmitForRender via SubmitLight)
        HlslTypes::HlslPerFrameConstants frameConstants = {};
        // Default exposure of 1.0x (0 stops) when no camera is active.
        frameConstants.Exposure = 1.0f;

        if (m_pActiveCamera)
        {
            auto viewProj = m_pActiveCamera->GetViewProjectionMatrix();
            memcpy(&frameConstants.ViewProj, &viewProj, sizeof(frameConstants.ViewProj));

            auto *pCameraNode = m_pActiveCamera->GetAttachedNode();
            if (pCameraNode)
            {
                auto camPos = pCameraNode->GetGlobalTranslation();
                memcpy(&frameConstants.CameraWorldPos, &camPos, sizeof(frameConstants.CameraWorldPos));

                // Camera basis from the world matrix (row-vector convention:
                // row 0 = forward, row 1 = right, row 2 = up).
                auto world = pCameraNode->GetGlobalMatrix();
                const float tanHalfFov = std::tan(0.5f * m_pActiveCamera->GetFovAngle());
                const float aspect     = m_pActiveCamera->GetAspectRatio();
                frameConstants.CamForwardAndTanHalfFov = { world[0][0], world[0][1], world[0][2], tanHalfFov };
                frameConstants.CamRightAndAspect       = { world[1][0], world[1][1], world[1][2], aspect    };
                frameConstants.CamUp                   = { world[2][0], world[2][1], world[2][2], 0.0f      };
            }

            frameConstants.Exposure = std::exp2(m_pActiveCamera->GetExposureStops());
        }

        frameConstants.LightCount = m_LightCount;
        frameConstants.LightCullThreshold = kDefaultLightCullThreshold;

        // Forward+ tile binning.  Must run after SubmitLight has
        // finished populating m_Lights (so spatial lights have valid
        // positions) and after ResolveShadowCasters has written its
        // bookkeeping back into m_Lights[].  The resulting tile grid
        // is uploaded below alongside the per-frame light table.
        {
            Canvas::Math::FloatMatrix4x4 vp{};
            if (m_pActiveCamera)
                vp = m_pActiveCamera->GetViewProjectionMatrix();
            const uint32_t fbW = static_cast<uint32_t>(m_BackBufferViewport.Width);
            const uint32_t fbH = static_cast<uint32_t>(m_BackBufferViewport.Height);
            BuildTileLightLists(fbW, fbH, vp);
        }
        frameConstants.LightTileCountX     = m_LightTileCountX;
        frameConstants.LightTileCountY     = m_LightTileCountY;
        frameConstants.LightTileSizePixels = LIGHT_TILE_SIZE_PIXELS;

        // Scene background -> per-frame constants.  The cube SRVs themselves
        // are bound below as part of the composite descriptor table.
        {
            const auto &bg = m_Background;
            frameConstants.SkySolidColor = { bg.SolidColor.X, bg.SolidColor.Y,
                                             bg.SolidColor.Z, bg.SolidColor.W };
            frameConstants.SkyOrientationQuat = { bg.Orientation.X, bg.Orientation.Y,
                                                  bg.Orientation.Z, bg.Orientation.W };
            frameConstants.SkyHasCubemap   = m_pSkyCubeA ? 1u : 0u;
            frameConstants.SkyHasCubemapB  = (m_pSkyCubeA && m_pSkyCubeB) ? 1u : 0u;
            frameConstants.SkyBlendFactor  = bg.BlendFactor;
            frameConstants.SkyIntensity    = bg.Intensity;

            // Stars overlay.
            frameConstants.StarsOrientationQuat =
                { bg.StarsOrientation.X, bg.StarsOrientation.Y,
                  bg.StarsOrientation.Z, bg.StarsOrientation.W };
            frameConstants.HasStars       = m_pStarsCube ? 1u : 0u;
            frameConstants.StarsIntensity = bg.StarsIntensity;

            // Sun procedural disc.  The engine precomputes cos(angularRadius)
            // so the per-pixel shader can use a single dot-product compare.
            const float cosSunRadius = std::cos((std::max)(bg.SunAngularRadius, 0.0f));
            frameConstants.SunDirAndCosRadius =
                { bg.SunDirection.X, bg.SunDirection.Y,
                  bg.SunDirection.Z, cosSunRadius };
            frameConstants.SunColorAndIntensity =
                { bg.SunColor.X, bg.SunColor.Y, bg.SunColor.Z, bg.SunColor.W };

            // Moon billboard.
            const float cosMoonRadius = std::cos((std::max)(bg.MoonAngularRadius, 0.0f));
            frameConstants.MoonDirAndCosRadius =
                { bg.MoonDirection.X, bg.MoonDirection.Y,
                  bg.MoonDirection.Z, cosMoonRadius };
            frameConstants.MoonColorAndIntensity =
                { bg.MoonColor.X, bg.MoonColor.Y, bg.MoonColor.Z, bg.MoonColor.W };
            frameConstants.HasMoon = m_pMoonTexture ? 1u : 0u;
        }

        // Add shadow-atlas global constants - composite needs the atlas
        // size and texel step to do PCF.  These are 0 / 0 when no shadow
        // atlas was allocated this frame, which the composite treats as
        // "no shadows" and short-circuits the sample.
        frameConstants.ShadowAtlasSize    = m_pShadowAtlas ? m_ShadowAtlasSize : 0u;
        frameConstants.ShadowPcfTexelStep = (m_ShadowAtlasSize > 0u)
            ? (1.0f / static_cast<float>(m_ShadowAtlasSize))
            : 0.0f;

        // Per-frame light table -- uploaded into the ring as a
        // structured buffer and bound to the composite root SRV at
        // slot 2 (t8).  Always allocate at least one element so the
        // root SRV is never bound to a null address (D3D12 validation
        // tolerates it but it's a sharper contract to keep the buffer
        // always valid; the shader's loop is gated by LightCount so
        // the dummy element is never read).
        const uint32_t lightUploadCount = (m_LightCount > 0) ? m_LightCount : 1u;
        const uint64_t lightUploadBytes =
            static_cast<uint64_t>(lightUploadCount) * sizeof(HlslTypes::HlslLight);
        HostWriteAllocation lightsHw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(lightUploadBytes, lightsHw));
        if (m_LightCount > 0)
            memcpy(lightsHw.pMapped, m_Lights.data(),
                   static_cast<size_t>(m_LightCount) * sizeof(HlslTypes::HlslLight));
        else
            memset(lightsHw.pMapped, 0, sizeof(HlslTypes::HlslLight));
        const D3D12_GPU_VIRTUAL_ADDRESS lightsSRVAddress = lightsHw.GpuAddress;

        // Per-tile light counts and packed indices for Forward+ tile
        // binning -- both rebuilt each frame by BuildTileLightLists
        // above.  Always allocate at least one element each so the
        // root SRVs are never bound to a null GPU address (the shader
        // gates reads by LightTileCountX/Y, so unused tail entries
        // are never sampled).
        const uint32_t totalTiles = m_LightTileCountX * m_LightTileCountY;
        const uint32_t tileCountElems = (totalTiles > 0) ? totalTiles : 1u;
        const uint64_t tileCountsBytes =
            static_cast<uint64_t>(tileCountElems) * sizeof(uint32_t);
        HostWriteAllocation tileCountsHw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(tileCountsBytes, tileCountsHw));
        if (totalTiles > 0)
            memcpy(tileCountsHw.pMapped, m_TileLightCounts.data(),
                   static_cast<size_t>(totalTiles) * sizeof(uint32_t));
        else
            memset(tileCountsHw.pMapped, 0, sizeof(uint32_t));
        const D3D12_GPU_VIRTUAL_ADDRESS tileCountsSRVAddress = tileCountsHw.GpuAddress;

        const uint32_t tileIndicesElems =
            (totalTiles > 0) ? (totalTiles * MAX_LIGHTS_PER_TILE) : 1u;
        const uint64_t tileIndicesBytes =
            static_cast<uint64_t>(tileIndicesElems) * sizeof(uint32_t);
        HostWriteAllocation tileIndicesHw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(tileIndicesBytes, tileIndicesHw));
        if (totalTiles > 0)
            memcpy(tileIndicesHw.pMapped, m_TileLightIndices.data(),
                   static_cast<size_t>(totalTiles) * MAX_LIGHTS_PER_TILE * sizeof(uint32_t));
        else
            memset(tileIndicesHw.pMapped, 0, sizeof(uint32_t));
        const D3D12_GPU_VIRTUAL_ADDRESS tileIndicesSRVAddress = tileIndicesHw.GpuAddress;

        // Upload per-frame constants and bind to root CBV (slot 0, register b0)
        constexpr uint64_t cbAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        const uint64_t cbSize = (sizeof(HlslTypes::HlslPerFrameConstants) + cbAlignment - 1) & ~(cbAlignment - 1);
        HostWriteAllocation hw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(cbSize, hw));
        
        memcpy(hw.pMapped, &frameConstants, sizeof(HlslTypes::HlslPerFrameConstants));
        
        D3D12_GPU_VIRTUAL_ADDRESS frameCBVAddress = hw.GpuAddress;

        // Bind per-frame constants for the geometry pass
        auto& frameConstTask = CreateGpuTask("FrameConstants");
        frameConstTask.RecordFunc = [frameCBVAddress](ID3D12GraphicsCommandList* pCL)
        {
            pCL->SetGraphicsRootConstantBufferView(0, frameCBVAddress);
        };
        m_GpuTaskGraph.InsertTask(frameConstTask);

        // Drain the renderable queue - process nodes inline
        for (auto *pNode : m_RenderableQueue)
        {
            UINT elementCount = pNode->GetBoundElementCount();
            for (UINT i = 0; i < elementCount; ++i)
            {
                auto *pElement = pNode->GetBoundElement(i);

                // Mesh instances: build per-object constants from node transform, draw internally
                Gem::TGemPtr<Canvas::XMeshInstance> pMeshInstance;
                if (SUCCEEDED(pElement->QueryInterface(&pMeshInstance)))
                {
                    auto *pMeshData = pMeshInstance->GetMeshData();
                    if (pMeshData)
                    {
                        const uint32_t groupCount = pMeshData->GetNumMaterialGroups();
                        const auto& worldMatrix = pNode->GetGlobalMatrix();
                        for (uint32_t g = 0; g < groupCount; ++g)
                        {
                            Gem::ThrowGemError(DrawMesh(pMeshData, g, worldMatrix, pMeshInstance.Get()));
                        }
                    }
                    continue;
                }

                // Lights were already accumulated during SubmitForRender - skip here
            }
        }
        m_RenderableQueue.clear();

        //==========================================================================================
        // Shadow pass: depth-only displaced-mesh draws into per-light atlas
        // tiles.  One CGpuTask per (resolved shadow caster x ready displaced
        // tile).  The first task targeting a given tile clears it; the
        // remaining tasks for that tile just accumulate depth.  Declared
        // resource usage (DEPTH_STENCIL_WRITE on the atlas + SHADER_RESOURCE
        // on each displacement map) drives automatic barriers; the composite's
        // SHADER_RESOURCE declaration on the atlas downstream finishes
        // the DSV-write -> SRV-read transition.
        //==========================================================================================
        bool anyResolvedShadowCaster = false;
        for (const auto& c : m_PendingShadowCasters)
            if (c.Resolved) { anyResolvedShadowCaster = true; break; }

        if (anyResolvedShadowCaster && !m_DisplacedDraws.empty())
        {
            EnsureDisplacedShadowPSO();
            // Re-create the atlas DSV every frame.  The DSV descriptor heap
            // is a fence-protected ring (m_DSVRing), and a cached handle
            // from a long-lived resource would eventually be overwritten by
            // a new DSV pointing to a different resource (m_pDepthBuffer in
            // particular) -- after that the shadow tasks would write the
            // scene depth buffer, breaking the geometry pass's depth test.
            m_ShadowAtlasDSV = CreateDepthStencilView(m_pShadowAtlas);

            ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();
            const UINT incSize = m_CbvSrvUavIncrement;

            const uint64_t shadowCbAlignedSize =
                (sizeof(Canvas::Math::FloatMatrix4x4) + cbAlignment - 1) & ~(cbAlignment - 1);
            const uint64_t displacedCbAlignedSize =
                (sizeof(HlslTypes::HlslDisplacedConstants) + cbAlignment - 1) & ~(cbAlignment - 1);

            auto isReady = [this](CSurface12* pSurf) -> bool
            {
                if (!pSurf || !pSurf->m_UploadFixupToken.IsValid())
                    return true;
                ID3D12Fence* pFence = m_pDevice->GetResourceManager().GetTimelineFence(
                    pSurf->m_UploadFixupToken.TimelineId);
                return pFence && pFence->GetCompletedValue() >= pSurf->m_UploadFixupToken.Value;
            };

            for (const auto& caster : m_PendingShadowCasters)
            {
                if (!caster.Resolved)
                    continue;   // dropped at resolution time -- no shadow region

                // Upload the caster's world->shadow-clip matrix once and
                // share its CBV across every tile it draws into this atlas tile.
                HostWriteAllocation shadowHw;
                Gem::ThrowGemError(m_UploadRing.AllocateFromRing(shadowCbAlignedSize, shadowHw));
                memcpy(shadowHw.pMapped, &caster.ShadowViewProj, sizeof(caster.ShadowViewProj));
                const D3D12_GPU_VIRTUAL_ADDRESS shadowCbvAddr = shadowHw.GpuAddress;

                const D3D12_VIEWPORT tileViewport = {
                    static_cast<FLOAT>(caster.TilePixelX),
                    static_cast<FLOAT>(caster.TilePixelY),
                    static_cast<FLOAT>(caster.TilePixelSize),
                    static_cast<FLOAT>(caster.TilePixelSize),
                    0.0f, 1.0f };
                const D3D12_RECT tileScissor = {
                    static_cast<LONG>(caster.TilePixelX),
                    static_cast<LONG>(caster.TilePixelY),
                    static_cast<LONG>(caster.TilePixelX + caster.TilePixelSize),
                    static_cast<LONG>(caster.TilePixelY + caster.TilePixelSize) };

                bool firstDrawInTile = true;

                for (const auto& sub : m_DisplacedDraws)
                {
                    auto* pMapSurf = static_cast<CSurface12*>(sub.pDisplacementMap);
                    if (!isReady(pMapSurf))
                        continue;

                    // Per-tile CB: shadow DS reuses HlslDisplacedConstants
                    // for the world transform + displacement decode.
                    HlslTypes::HlslDisplacedConstants tileCb = {};
                    memcpy(&tileCb.World, &sub.World, sizeof(tileCb.World));
                    tileCb.MapScale = sub.MapScale;
                    tileCb.MapBias  = sub.MapBias;

                    HostWriteAllocation tileHw;
                    Gem::ThrowGemError(m_UploadRing.AllocateFromRing(displacedCbAlignedSize, tileHw));
                    memcpy(tileHw.pMapped, &tileCb, sizeof(tileCb));
                    const D3D12_GPU_VIRTUAL_ADDRESS perTileCbvAddr = tileHw.GpuAddress;

                    // Four contiguous SRV slots: t0 displacement map,
                    // t4..t6 positions / UVs / normals.  Two descriptor
                    // tables index into the same block (t0 at base, CP
                    // streams at base+1..base+3).
                    constexpr UINT kShadowSRVCount = 4;
                    const UINT baseSrvSlot = AllocateSRVSlots(kShadowSRVCount);
                    {
                        ID3D12Resource* pRes = pMapSurf->GetD3DResource();
                        D3D12_RESOURCE_DESC desc = pRes->GetDesc();
                        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
                        srv.Format                        = desc.Format;
                        srv.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srv.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        srv.Texture2D.MostDetailedMip     = 0;
                        srv.Texture2D.MipLevels           = desc.MipLevels;
                        srv.Texture2D.PlaneSlice          = 0;
                        srv.Texture2D.ResourceMinLODClamp = 0.0f;
                        CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(
                            m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                            baseSrvSlot, incSize);
                        pD3DDevice->CreateShaderResourceView(pRes, &srv, cpu);
                    }
                    auto writeStructuredBufSRV = [&](UINT slotOffset, ID3D12Resource* pRes, UINT stride)
                    {
                        D3D12_RESOURCE_DESC rd = pRes->GetDesc();
                        D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
                        d.Format                  = DXGI_FORMAT_UNKNOWN;
                        d.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
                        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        d.Buffer.NumElements      = static_cast<UINT>(rd.Width / stride);
                        d.Buffer.StructureByteStride = stride;
                        CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(
                            m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                            baseSrvSlot + slotOffset, incSize);
                        pD3DDevice->CreateShaderResourceView(pRes, &d, cpu);
                    };
                    writeStructuredBufSRV(1, sub.pPositions->GetD3DResource(), sizeof(Canvas::Math::FloatVector4));
                    writeStructuredBufSRV(2, sub.pUV0s     ->GetD3DResource(), sizeof(Canvas::Math::FloatVector2));
                    writeStructuredBufSRV(3, sub.pNormals  ->GetD3DResource(), sizeof(Canvas::Math::FloatVector4));

                    CD3DX12_GPU_DESCRIPTOR_HANDLE mapGpu(
                        m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
                        baseSrvSlot, incSize);
                    CD3DX12_GPU_DESCRIPTOR_HANDLE cpGpu(
                        m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
                        baseSrvSlot + 1, incSize);

                    const uint32_t cpVertexCount = sub.CPVertexCount;
                    const bool doClear = firstDrawInTile;
                    firstDrawInTile = false;

                    auto& shadowTask = CreateGpuTask("ShadowPass_Displaced");
                    DeclareGpuTextureUsage(shadowTask, pMapSurf,
                        D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                        D3D12_BARRIER_SYNC_VERTEX_SHADING,   // HS + DS sample
                        D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
                    DeclareGpuTextureUsage(shadowTask, m_pShadowAtlas.Get(),
                        D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
                        D3D12_BARRIER_SYNC_DEPTH_STENCIL,
                        D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);

                    shadowTask.RecordFunc = [this, frameCBVAddress, perTileCbvAddr,
                                             shadowCbvAddr, mapGpu, cpGpu, cpVertexCount,
                                             tileViewport, tileScissor, doClear]
                                            (ID3D12GraphicsCommandList* pCL)
                    {
                        pCL->SetPipelineState(m_pDisplacedShadowPSO);
                        pCL->SetGraphicsRootSignature(m_pDisplacedShadowRootSig);
                        pCL->OMSetRenderTargets(0, nullptr, FALSE, &m_ShadowAtlasDSV);
                        pCL->RSSetViewports(1, &tileViewport);
                        pCL->RSSetScissorRects(1, &tileScissor);
                        if (doClear)
                        {
                            // Reverse-Z far plane = 0.0; scissor-restricted
                            // clear so we only touch this tile.
                            pCL->ClearDepthStencilView(m_ShadowAtlasDSV,
                                D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 1, &tileScissor);
                        }
                        pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
                        pCL->SetDescriptorHeaps(2, m_DescriptorHeapsArray);
                        pCL->SetGraphicsRootConstantBufferView(0, frameCBVAddress);
                        pCL->SetGraphicsRootConstantBufferView(1, perTileCbvAddr);
                        pCL->SetGraphicsRootConstantBufferView(2, shadowCbvAddr);
                        pCL->SetGraphicsRootDescriptorTable(3, mapGpu);
                        pCL->SetGraphicsRootDescriptorTable(4, cpGpu);
                        pCL->DrawInstanced(cpVertexCount, 1, 0, 0);

                        // Restore the back-buffer viewport + scissor so
                        // downstream geometry + composite tasks render into
                        // the intended visible region; without this they
                        // inherit the atlas tile rect.
                        pCL->RSSetViewports(1, &m_BackBufferViewport);
                        pCL->RSSetScissorRects(1, &m_BackBufferScissor);
                    };
                    m_GpuTaskGraph.InsertTask(shadowTask);
                }
            }
        }

        //==========================================================================================
        // Displaced-mesh draws: GPU-tessellated patch lists writing into the
        // G-buffer. Same MRT shape as the default geometry pass, so the
        // composite picks up displaced pixels uniformly with the rest of the
        // scene.
        //==========================================================================================
        if (!m_DisplacedDraws.empty())
        {
            EnsureDisplacedPSO();   // make sure both root sig + PSO are available
            ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();
            const UINT incSize = m_CbvSrvUavIncrement;

            const uint64_t displacedCbSize = (sizeof(HlslTypes::HlslDisplacedConstants) + cbAlignment - 1) & ~(cbAlignment - 1);

            for (const auto& sub : m_DisplacedDraws)
            {
                // Don't bind the displacement map until its upload + COMMON->SHADER_RESOURCE
                // fixup CL has fully retired on the GPU. The displaced root sig
                // declares the map SRV range as DATA_STATIC, which forbids
                // any state change on the resource while a CL with the binding
                // is in flight. By waiting for the fixup token to retire, we
                // guarantee the layout transition is in the past relative to
                // any submission that follows. Tiles whose maps are not
                // yet ready are simply skipped this frame; the scene graph will
                // re-submit them next frame.
                // All four displaced-draw-bound surfaces must have their COMMON ->
                // SHADER_RESOURCE fixup CL retired on the GPU before we can
                // bind them as DATA_STATIC SRVs. Tiles whose displacement map or
                // material atlases are not yet ready are simply skipped this
                // frame; the scene graph re-submits them next frame.
                auto* pMapSurf    = static_cast<CSurface12*>(sub.pDisplacementMap);
                auto* pAlbedoSurf = static_cast<CSurface12*>(sub.pAlbedo);
                auto* pAOSurf     = static_cast<CSurface12*>(sub.pAOMap);
                auto* pRoughSurf  = static_cast<CSurface12*>(sub.pRoughnessMap);

                auto isReady = [this](CSurface12* pSurf) -> bool
                {
                    if (!pSurf || !pSurf->m_UploadFixupToken.IsValid())
                        return true;
                    ID3D12Fence* pFence = m_pDevice->GetResourceManager().GetTimelineFence(
                        pSurf->m_UploadFixupToken.TimelineId);
                    return pFence && pFence->GetCompletedValue() >= pSurf->m_UploadFixupToken.Value;
                };
                if (!isReady(pMapSurf)    || !isReady(pAlbedoSurf) ||
                    !isReady(pAOSurf)     || !isReady(pRoughSurf))
                {
                    continue;
                }

                // ---- Per-tile constant buffer ----
                HlslTypes::HlslDisplacedConstants tileCb = {};
                memcpy(&tileCb.World, &sub.World, sizeof(tileCb.World));
                tileCb.MapScale = sub.MapScale;
                tileCb.MapBias  = sub.MapBias;

                HostWriteAllocation tileHw;
                Gem::ThrowGemError(m_UploadRing.AllocateFromRing(displacedCbSize, tileHw));
                memcpy(tileHw.pMapped, &tileCb, sizeof(tileCb));
                D3D12_GPU_VIRTUAL_ADDRESS perTileCbvAddr = tileHw.GpuAddress;

                // ---- SRVs: 7 contiguous slots (displacement map, 3 atlases, 3 CP streams) ----
                // Three descriptor tables index into this block:
                //   base+0      -> t0  displacement map     (visibility ALL)
                //   base+1..+3  -> t1..t3 atlases           (visibility PIXEL)
                //   base+4..+6  -> t4..t6 pos / UV / normal (visibility VERTEX)
                constexpr UINT kDisplacedSRVCount = 7;
                UINT baseSrvSlot = AllocateSRVSlots(kDisplacedSRVCount);

                CD3DX12_GPU_DESCRIPTOR_HANDLE mapGpu(
                    m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
                    baseSrvSlot, incSize);
                CD3DX12_GPU_DESCRIPTOR_HANDLE materialGpu(
                    m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
                    baseSrvSlot + 1, incSize);
                CD3DX12_GPU_DESCRIPTOR_HANDLE cpGpu(
                    m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
                    baseSrvSlot + 4, incSize);

                auto createTex2DSRV = [&](CSurface12* pSurf, UINT slotOffset)
                {
                    ID3D12Resource* pRes = pSurf->GetD3DResource();
                    D3D12_RESOURCE_DESC desc = pRes->GetDesc();
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
                    srv.Format                    = desc.Format;
                    srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srv.Texture2D.MostDetailedMip = 0;
                    srv.Texture2D.MipLevels       = desc.MipLevels;
                    srv.Texture2D.PlaneSlice      = 0;
                    srv.Texture2D.ResourceMinLODClamp = 0.0f;
                    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(
                        m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                        baseSrvSlot + slotOffset, incSize);
                    pD3DDevice->CreateShaderResourceView(pRes, &srv, cpu);
                };
                auto createStructuredBufSRV = [&](ID3D12Resource* pRes, UINT stride, UINT slotOffset)
                {
                    D3D12_RESOURCE_DESC rd = pRes->GetDesc();
                    D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
                    d.Format                     = DXGI_FORMAT_UNKNOWN;
                    d.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
                    d.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    d.Buffer.NumElements         = static_cast<UINT>(rd.Width / stride);
                    d.Buffer.StructureByteStride = stride;
                    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(
                        m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                        baseSrvSlot + slotOffset, incSize);
                    pD3DDevice->CreateShaderResourceView(pRes, &d, cpu);
                };
                createTex2DSRV(pMapSurf,    0);
                createTex2DSRV(pAlbedoSurf, 1);
                createTex2DSRV(pAOSurf,     2);
                createTex2DSRV(pRoughSurf,  3);
                createStructuredBufSRV(sub.pPositions->GetD3DResource(), sizeof(Canvas::Math::FloatVector4), 4);
                createStructuredBufSRV(sub.pUV0s     ->GetD3DResource(), sizeof(Canvas::Math::FloatVector2), 5);
                createStructuredBufSRV(sub.pNormals  ->GetD3DResource(), sizeof(Canvas::Math::FloatVector4), 6);

                const uint32_t cpVertexCount = sub.CPVertexCount;

                auto& drawTask = CreateGpuTask("DrawDisplaced");
                // SYNC_VERTEX_SHADING covers VS / HS / DS / GS under the
                // enhanced-barriers grouping. The displacement map is sampled
                // in the domain shader; the material atlases are sampled in
                // the pixel shader (SYNC_PIXEL_SHADING).
                DeclareGpuTextureUsage(drawTask, pMapSurf,
                    D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                    // SYNC_VERTEX_SHADING covers VS / HS / DS / GS. The HS
                    // reads the displacement map for distance + curvature
                    // LOD, the DS reads it for the displacement lift.
                    D3D12_BARRIER_SYNC_VERTEX_SHADING,
                    D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
                DeclareGpuTextureUsage(drawTask, pAlbedoSurf,
                    D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                    D3D12_BARRIER_SYNC_PIXEL_SHADING,
                    D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
                DeclareGpuTextureUsage(drawTask, pAOSurf,
                    D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                    D3D12_BARRIER_SYNC_PIXEL_SHADING,
                    D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
                DeclareGpuTextureUsage(drawTask, pRoughSurf,
                    D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                    D3D12_BARRIER_SYNC_PIXEL_SHADING,
                    D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

                drawTask.RecordFunc = [this, frameCBVAddress, perTileCbvAddr,
                                       mapGpu, materialGpu, cpGpu, cpVertexCount]
                                      (ID3D12GraphicsCommandList* pCL)
                {
                    pCL->SetPipelineState(GetActiveDisplacedPSO());
                    pCL->SetGraphicsRootSignature(m_pDisplacedRootSig);
                    pCL->OMSetRenderTargets(5, m_GBufferRTVs, FALSE, &m_CurrentDSV);
                    pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
                    pCL->SetDescriptorHeaps(2, m_DescriptorHeapsArray);
                    pCL->SetGraphicsRootConstantBufferView(0, frameCBVAddress);
                    pCL->SetGraphicsRootConstantBufferView(1, perTileCbvAddr);
                    pCL->SetGraphicsRootDescriptorTable(2, mapGpu);
                    pCL->SetGraphicsRootDescriptorTable(3, materialGpu);
                    pCL->SetGraphicsRootDescriptorTable(4, cpGpu);
                    pCL->DrawInstanced(cpVertexCount, 1, 0, 0);
                };
                m_GpuTaskGraph.InsertTask(drawTask);
            }
        }
        m_DisplacedDraws.clear();

        //==========================================================================================
        // Composition pass: read G-buffers + scene background, perform deferred lighting,
        // write to back buffer.  The composite owns every output pixel (background fills
        // empty G-buffer pixels), so the render-target clear is no longer required.
        //==========================================================================================
        {
            CSurface12* pBackBuffer = m_pCurrentSwapChain->m_pSurface;

            // CPU work: allocate SRV descriptors and create SRVs.  Slots 0-2 are the
            // G-buffer Texture2D SRVs (always present); slots 3-4 are skybox cube
            // SRVs; slot 5 is the optional stars cube; slot 6 is the optional moon
            // 2D billboard texture.  Unbound slots hold null SRVs (cube or 2D as
            // appropriate) so the descriptor table stays well defined; the shader's
            // SkyHasCubemap / HasStars / HasMoon flags select active branches.
            ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();
            const UINT incSize = m_CbvSrvUavIncrement;

            constexpr UINT kCompositeSRVCount = 8;
            UINT baseSlot = AllocateSRVSlots(kCompositeSRVCount);

            CD3DX12_CPU_DESCRIPTOR_HANDLE baseCpuHandle(m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), baseSlot, incSize);
            CD3DX12_GPU_DESCRIPTOR_HANDLE baseGpuHandle(m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), baseSlot, incSize);

            pD3DDevice->CreateShaderResourceView(m_pGBufferNormals->GetD3DResource(), nullptr, baseCpuHandle);
            CD3DX12_CPU_DESCRIPTOR_HANDLE diffuseSrvHandle(baseCpuHandle, 1, incSize);
            pD3DDevice->CreateShaderResourceView(m_pGBufferDiffuseColor->GetD3DResource(), nullptr, diffuseSrvHandle);
            CD3DX12_CPU_DESCRIPTOR_HANDLE worldPosSrvHandle(baseCpuHandle, 2, incSize);
            pD3DDevice->CreateShaderResourceView(m_pGBufferWorldPos->GetD3DResource(), nullptr, worldPosSrvHandle);

            // Skybox cube SRVs at t3 / t4.  D3D12 permits a null resource paired
            // with an explicit SRV desc; sampling such a descriptor returns zero,
            // which is harmless because the shader's SkyHasCubemap flag bypasses
            // the sample entirely when no cube is bound.
            auto createCubeSRV = [&](Canvas::XGfxSurface* pSurf, UINT slotOffset)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
                srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURECUBE;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                ID3D12Resource* pRes = nullptr;
                if (pSurf)
                {
                    auto* pCube = static_cast<CSurface12*>(pSurf);
                    pRes = pCube->GetD3DResource();
                    D3D12_RESOURCE_DESC rd = pRes->GetDesc();
                    srv.Format                          = rd.Format;
                    srv.TextureCube.MostDetailedMip     = 0;
                    srv.TextureCube.MipLevels           = rd.MipLevels;
                    srv.TextureCube.ResourceMinLODClamp = 0.0f;
                }
                else
                {
                    // Null SRV must still specify a concrete format.
                    srv.Format                          = DXGI_FORMAT_R8G8B8A8_UNORM;
                    srv.TextureCube.MostDetailedMip     = 0;
                    srv.TextureCube.MipLevels           = 1;
                    srv.TextureCube.ResourceMinLODClamp = 0.0f;
                }
                CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(baseCpuHandle, slotOffset, incSize);
                pD3DDevice->CreateShaderResourceView(pRes, &srv, cpu);
            };
            createCubeSRV(m_pSkyCubeA.Get(), 3);
            createCubeSRV(m_pSkyCubeB.Get(), 4);
            createCubeSRV(m_pStarsCube.Get(), 5);

            // Moon 2D billboard SRV at t6.  Same null-with-explicit-desc
            // approach as the cubes above.
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
                srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                ID3D12Resource* pRes = nullptr;
                if (m_pMoonTexture)
                {
                    auto* pTex = static_cast<CSurface12*>(m_pMoonTexture.Get());
                    pRes = pTex->GetD3DResource();
                    D3D12_RESOURCE_DESC rd = pRes->GetDesc();
                    srv.Format                          = rd.Format;
                    srv.Texture2D.MostDetailedMip       = 0;
                    srv.Texture2D.MipLevels             = rd.MipLevels;
                    srv.Texture2D.PlaneSlice            = 0;
                    srv.Texture2D.ResourceMinLODClamp   = 0.0f;
                }
                else
                {
                    srv.Format                          = DXGI_FORMAT_R8G8B8A8_UNORM;
                    srv.Texture2D.MostDetailedMip       = 0;
                    srv.Texture2D.MipLevels             = 1;
                    srv.Texture2D.PlaneSlice            = 0;
                    srv.Texture2D.ResourceMinLODClamp   = 0.0f;
                }
                CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(baseCpuHandle, 6, incSize);
                pD3DDevice->CreateShaderResourceView(pRes, &srv, cpu);
            }

            // Shadow atlas SRV at t7.  Created as R32_Float so the
            // depth surface can be sampled via SamplerComparisonState
            // (D32_Float is not a valid SRV format).  When no atlas
            // exists this frame (no shadow casters), a null SRV with
            // the same format keeps the descriptor table well defined;
            // per-light ShadowFlags == 0 in the shader skips the sample.
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
                srv.Format                          = DXGI_FORMAT_R32_FLOAT;
                srv.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Texture2D.MostDetailedMip       = 0;
                srv.Texture2D.MipLevels             = 1;
                srv.Texture2D.PlaneSlice            = 0;
                srv.Texture2D.ResourceMinLODClamp   = 0.0f;
                ID3D12Resource* pRes = m_pShadowAtlas
                    ? m_pShadowAtlas->GetD3DResource()
                    : nullptr;
                CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(baseCpuHandle, 7, incSize);
                pD3DDevice->CreateShaderResourceView(pRes, &srv, cpu);
            }

            // Composite task: transition G-buffers + skybox cubes to SR, back buffer to RT, then draw
            auto& compositeTask = CreateGpuTask("CompositePass");
            DeclareGpuTextureUsage(compositeTask, m_pGBufferNormals,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            DeclareGpuTextureUsage(compositeTask, m_pGBufferDiffuseColor,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            DeclareGpuTextureUsage(compositeTask, m_pGBufferWorldPos,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            if (m_pSkyCubeA)
            {
                DeclareGpuTextureUsage(compositeTask, static_cast<CSurface12*>(m_pSkyCubeA.Get()),
                    D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                    D3D12_BARRIER_SYNC_PIXEL_SHADING,
                    D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            }
            if (m_pSkyCubeB)
            {
                DeclareGpuTextureUsage(compositeTask, static_cast<CSurface12*>(m_pSkyCubeB.Get()),
                    D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                    D3D12_BARRIER_SYNC_PIXEL_SHADING,
                    D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            }
            if (m_pStarsCube)
            {
                DeclareGpuTextureUsage(compositeTask, static_cast<CSurface12*>(m_pStarsCube.Get()),
                    D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                    D3D12_BARRIER_SYNC_PIXEL_SHADING,
                    D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            }
            if (m_pMoonTexture)
            {
                DeclareGpuTextureUsage(compositeTask, static_cast<CSurface12*>(m_pMoonTexture.Get()),
                    D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                    D3D12_BARRIER_SYNC_PIXEL_SHADING,
                    D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            }
            // Shadow atlas usage: composite samples it via PCF in the
            // directional-light branch.  The DSV_WRITE -> SHADER_RESOURCE
            // transition that follows the shadow-pass tasks is emitted
            // automatically by the GpuTaskGraph from this declaration.
            if (m_pShadowAtlas)
            {
                DeclareGpuTextureUsage(compositeTask, m_pShadowAtlas.Get(),
                    D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                    D3D12_BARRIER_SYNC_PIXEL_SHADING,
                    D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            }
            DeclareGpuTextureUsage(compositeTask, pBackBuffer,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            compositeTask.RecordFunc = [frameCBVAddress, lightsSRVAddress, tileCountsSRVAddress, tileIndicesSRVAddress, baseGpuHandle, this](ID3D12GraphicsCommandList* pCL)
            {
                // No clear: every back-buffer pixel is written by the composite PS
                // (background fills empty G-buffer pixels).
                pCL->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, nullptr);
                pCL->SetGraphicsRootSignature(m_pCompositeRootSig);
                pCL->SetPipelineState(m_pCompositePSO);
                // Explicit topology: prior draws (e.g. displaced) may have left
                // the IA in PATCHLIST. Composite PSO expects TRIANGLE.
                pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                pCL->SetDescriptorHeaps(2, m_DescriptorHeapsArray);
                pCL->SetGraphicsRootConstantBufferView(0, frameCBVAddress);
                pCL->SetGraphicsRootDescriptorTable(1, baseGpuHandle);
                pCL->SetGraphicsRootShaderResourceView(2, lightsSRVAddress);
                pCL->SetGraphicsRootShaderResourceView(3, tileCountsSRVAddress);
                pCL->SetGraphicsRootShaderResourceView(4, tileIndicesSRVAddress);
                pCL->DrawInstanced(3, 1, 0, 0);
            };
            m_GpuTaskGraph.InsertTask(compositeTask);
        }

        // Flush pending glyph atlas uploads (SDF bitmaps -> GPU texture)
        FlushPendingGlyphUploads();

        // Draw UI elements (pure draw - uploads already staged by SubmitRenderables)
        for (auto* pNode : m_UIRenderableQueue)
        {
            UINT elemCount = pNode->GetBoundElementCount();
            for (UINT i = 0; i < elemCount; ++i)
            {
                auto* pElem = pNode->GetBoundElement(i);
                if (!pElem->IsVisible())
                    continue;

                Canvas::Math::FloatVector2 elementOffset = pNode->GetGlobalPosition();
                auto& localOffset = pElem->GetLocalOffset();
                elementOffset.X += localOffset.X;
                elementOffset.Y += localOffset.Y;

                if (pElem->GetType() == Canvas::UIElementType::Rect)
                {
                    Gem::TGemPtr<Canvas::XUIRectElement> pRect;
                    if (SUCCEEDED(pElem->QueryInterface(&pRect)))
                    {
                        auto& size = pRect->GetSize();
                        if (size.X > 0.0f && size.Y > 0.0f)
                            Gem::ThrowGemError(DrawUIRect(size, pRect->GetFillColor(), elementOffset));
                    }
                }
                else if (pElem->GetType() == Canvas::UIElementType::Text)
                {
                    if (!pElem->HasContent())
                        continue;

                    auto* pTextImpl = static_cast<CUITextElement12*>(static_cast<Canvas::XUITextElement*>(pElem));
                    if (pTextImpl->GetGlyphCount() > 0)
                    {
                        Canvas::GfxResourceAllocation glyphSRV = pTextImpl->GetGlyphBuffer();
                        Gem::ThrowGemError(m_pDevice->AllocateStructuredBuffer(
                            pTextImpl->GetGlyphCount(), sizeof(HlslTypes::HlslGlyphInstance),
                            pTextImpl->GetGlyphData(), this, glyphSRV));
                        pTextImpl->SetGlyphBuffer(glyphSRV);

                        DeferRelease(glyphSRV.pBuffer.Get());

                        auto* pAtlas = m_pDevice->GetGlyphAtlasSurface();
                        if (pAtlas)
                        {
                            DeferRelease(pAtlas);
                            Gem::ThrowGemError(DrawUIText(glyphSRV, pAtlas, pTextImpl->GetLayoutConfig().Color, elementOffset));
                        }
                    }
                }
            }
        }
        m_UIRenderableQueue.clear();
    }
    catch (Gem::GemError &e)
    {
        m_RenderableQueue.clear();
        m_UIRenderableQueue.clear();
        m_DisplacedDraws.clear();
        m_PendingShadowCasters.clear();
        m_FrameWorldBounds.Reset();
        m_VisibleLightFilter.clear();
        m_HasVisibleLightFilter = false;
        m_Lights.clear();
        m_LightCount = 0;
        m_TileLightCounts.clear();
        m_TileLightIndices.clear();
        m_LightTileCountX = 0;
        m_LightTileCountY = 0;
        m_pCurrentSwapChain = nullptr;
        m_pActiveCamera = nullptr;
        return e.Result();
    }

    m_pCurrentSwapChain = nullptr;
    m_pActiveCamera = nullptr;
    m_LightCount = 0;
    m_Lights.clear();
    m_TileLightCounts.clear();
    m_TileLightIndices.clear();
    m_LightTileCountX = 0;
    m_LightTileCountY = 0;
    m_NextShadowTileIndex = 0;
    m_PendingShadowCasters.clear();
    m_FrameWorldBounds.Reset();
    // Light visibility filter is per-frame: clear here so the next
    // frame starts in "no filter" mode unless Scene installs one
    // again.  Matches the contract documented on SetVisibleLights.
    m_VisibleLightFilter.clear();
    m_HasVisibleLightFilter = false;
    return Gem::Result::Success;
}

