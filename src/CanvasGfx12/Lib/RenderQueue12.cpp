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
#include "GlyphAtlas.h"

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

    // Initialize task graphs — all start with work CLs closed
    m_GpuTaskGraph.Init(pD3DDevice, pCQ, &m_AllocatorPool);
    m_UIGpuTaskGraph.Init(pD3DDevice, pCQ, &m_AllocatorPool);
    m_PresentGpuTaskGraph.Init(pD3DDevice, pCQ, &m_AllocatorPool);

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
    m_pCommandQueue.Attach(pCQ.Detach());
    m_pFence.Attach(pFence.Detach());

    m_CbvSrvUavIncrement = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_SamplerIncrement   = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    m_RtvIncrement       = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_DsvIncrement       = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    m_DescriptorHeapsArray[0] = m_pShaderResourceDescriptorHeap;
    m_DescriptorHeapsArray[1] = m_pSamplerDescriptorHeap;

    // Per-queue upload ring (1 MB initial, grows on demand).
    m_UploadRing.Initialize(pDevice, 1 * 1024 * 1024);

    // Register this queue's fence with the device-level resource manager so
    // pooled buffers and deferred releases can be tracked queue-agnostically.
    m_TimelineId = pDevice->GetResourceManager().RegisterTimeline(m_pFence);

    // Pre-allocate per-frame queues to avoid repeated heap reallocations
    m_PendingBufferUploads.reserve(64);
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
    UINT slot = m_NextRTVSlot;
    m_NextRTVSlot = (m_NextRTVSlot + 1) % NumRTVDescriptors;
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

    // Dispatch order: scene → UI
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

        // Back buffer → COMMON for Present. Always dispatched last via the present graph.
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

    // GPU is idle — drain any remaining deferred resources.
    ProcessCompletedWork();

    // Unregister this queue's timeline from the device-level resource manager.
    // Drains and drops any retired buffers / deferred refs owned by this timeline.
    // The shared bucketed pool stays alive — it is owned by the device and torn
    // down when the device is destroyed (or by the last surviving queue's release
    // chain via CDevice12::~CDevice12).
    if (m_TimelineId != FenceToken::kInvalidTimelineId)
    {
        m_pDevice->GetResourceManager().UnregisterTimeline(m_TimelineId);
        m_TimelineId = FenceToken::kInvalidTimelineId;
    }

    // Release the per-queue upload ring now that the GPU is idle.
    m_UploadRing.Shutdown();
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
    UINT slot = m_NextDSVSlot;
    m_NextDSVSlot = (m_NextDSVSlot + 1) % NumDSVDescriptors;
    
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
    UINT slot = m_NextSRVSlot;
    m_NextSRVSlot = (m_NextSRVSlot + 1) % NumShaderResourceDescriptors;
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), slot, m_CbvSrvUavIncrement);
    
    pD3DDevice->CreateShaderResourceView(pResource, &srvDesc, cpuHandle);
    
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), slot, m_CbvSrvUavIncrement);
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
    
    // Create PSO - geometry pass writes to G-buffer MRTs (normals + diffuse + world pos)
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
    
    // MRT: three G-buffer render targets (normals + diffuse color + world position)
    psoDesc.NumRenderTargets = 3;
    psoDesc.RTVFormats[0] = CanvasFormatToDXGIFormat(m_GBufferNormalsFormat);
    psoDesc.RTVFormats[1] = CanvasFormatToDXGIFormat(m_GBufferDiffuseFormat);
    psoDesc.RTVFormats[2] = CanvasFormatToDXGIFormat(m_GBufferWorldPosFormat);
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    
    ThrowFailedHResult(m_pDevice->GetD3DDevice()->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_pDefaultPSO)));
    
    Canvas::LogInfo(m_pDevice->GetLogger(), "Geometry pass PSO created (3 MRT G-buffers)");
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
    //   Slot 0: Root CBV(b0) – TextScreenConstants (screen width/height)
    //   Slot 1: Root SRV(t0) – StructuredBuffer<TextVertex> (no atlas needed)
    std::vector<CD3DX12_ROOT_PARAMETER1> rectRootParams(2);
    rectRootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                                               D3D12_SHADER_VISIBILITY_VERTEX);
    rectRootParams[1].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
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
    //   Slot 0: Root CBV (b0) — lighting / per-frame constants
    //   Slot 1: Descriptor table with SRV[3] at t0-t2 — G-buffer textures
    //   Static sampler s0: point, clamp (exact texel fetch)
    CD3DX12_STATIC_SAMPLER_DESC pointSampler(
        0,                                      // shader register s0
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0,  // SRV[3] at t0-t2, space0
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

        // Route to whichever task graph is currently active
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
        
        // CPU work: create descriptors and compute viewport/scissor
        m_GBufferRTVs[0] = CreateRenderTargetView(m_pGBufferNormals, 0, 0, 0);
        m_GBufferRTVs[1] = CreateRenderTargetView(m_pGBufferDiffuseColor, 0, 0, 0);
        m_GBufferRTVs[2] = CreateRenderTargetView(m_pGBufferWorldPos, 0, 0, 0);
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
            pCL->ClearDepthStencilView(m_CurrentDSV, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
            pCL->OMSetRenderTargets(3, m_GBufferRTVs, FALSE, &m_CurrentDSV);
            pCL->RSSetViewports(1, &viewport);
            pCL->RSSetScissorRects(1, &scissor);
            pCL->SetPipelineState(m_pDefaultPSO);
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
    const Canvas::Math::FloatMatrix4x4 &worldTransform)
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

        // Pack per-object constants from the world transform
        HlslTypes::HlslPerObjectConstants objectConstants = {};
        static_assert(sizeof(objectConstants.World) == sizeof(worldTransform),
                      "HlslTypes::float4x4 and Math::FloatMatrix4x4 must be layout-compatible");
        memcpy(&objectConstants.World, &worldTransform, sizeof(objectConstants.World));
        memcpy(&objectConstants.WorldInvTranspose, &worldTransform, sizeof(objectConstants.WorldInvTranspose));
        // Zero translation row for normal transform (inverse transpose of upper 3x3)
        objectConstants.WorldInvTranspose.m[3][0] = 0.0f;
        objectConstants.WorldInvTranspose.m[3][1] = 0.0f;
        objectConstants.WorldInvTranspose.m[3][2] = 0.0f;
        objectConstants.WorldInvTranspose.m[3][3] = 1.0f;
        
        // Upload per-object constants to upload heap
        // CBVs require 256-byte aligned BufferLocation (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)
        constexpr uint64_t cbAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        const uint64_t cbSize = (sizeof(HlslTypes::HlslPerObjectConstants) + cbAlignment - 1) & ~(cbAlignment - 1);
        HostWriteAllocation hw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(cbSize, hw));
        
        memcpy(hw.pMapped, &objectConstants, sizeof(HlslTypes::HlslPerObjectConstants));
        
        // Allocate a contiguous block of 8 descriptors (2 CBV + 4 SRV + 2 UAV).
        // Wrap to slot 0 if the block would straddle the ring boundary to avoid
        // writing past the end of the heap.
        if (m_NextSRVSlot + 8 > NumShaderResourceDescriptors)
            m_NextSRVSlot = 0;
        UINT baseSlot = m_NextSRVSlot;
        m_NextSRVSlot += 8;
        
        const UINT incSize = m_CbvSrvUavIncrement;
        ID3D12Device *pD3DDevice = m_pDevice->GetD3DDevice();
        CD3DX12_CPU_DESCRIPTOR_HANDLE baseCpuHandle(m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), baseSlot, incSize);
        CD3DX12_GPU_DESCRIPTOR_HANDLE baseGpuHandle(m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), baseSlot, incSize);
        
        // Slot 0 of table: CBV for per-object constants (b1)
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = hw.GpuAddress;
        cbvDesc.SizeInBytes = static_cast<UINT>((sizeof(HlslTypes::HlslPerObjectConstants) + 255) & ~255); // 256-byte aligned
        pD3DDevice->CreateConstantBufferView(&cbvDesc, baseCpuHandle);
        
        // Slot 1 of table: CBV[1] placeholder (b2) - null
        CD3DX12_CPU_DESCRIPTOR_HANDLE cbv1Handle(baseCpuHandle, 1, incSize);
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
            CD3DX12_CPU_DESCRIPTOR_HANDLE normSrvHandle(baseCpuHandle, 2, incSize);
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
            pD3DDevice->CreateShaderResourceView(nullptr, &nullSrvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(baseCpuHandle, 2, incSize));
        }
        
        // Null SRVs for t2-t4 (slots 3-5)
        for (UINT i = 3; i <= 5; ++i)
            pD3DDevice->CreateShaderResourceView(nullptr, &nullSrvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(baseCpuHandle, i, incSize));
        
        // Null UAVs for u1-u2 (slots 6-7)
        D3D12_UNORDERED_ACCESS_VIEW_DESC nullUavDesc = {};
        nullUavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        nullUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        for (UINT i = 6; i <= 7; ++i)
            pD3DDevice->CreateUnorderedAccessView(nullptr, nullptr, &nullUavDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(baseCpuHandle, i, incSize));
        
        // Set root SRV (slot 1) for positions (t0)
        D3D12_GPU_VIRTUAL_ADDRESS posGpuAddr = pPosBuf->GetD3DResource()->GetGPUVirtualAddress();
        
        // Determine vertex count from position buffer size
        D3D12_RESOURCE_DESC posDesc = pPosBuf->GetD3DResource()->GetDesc();
        UINT vertexCount = static_cast<UINT>(posDesc.Width / sizeof(Canvas::Math::FloatVector4));
        
        // Create a task for the draw call
        auto& drawTask = CreateGpuTask("DrawMesh");
        m_GpuTaskGraph.DeclareBufferUsage(drawTask, pPosBuf,
            D3D12_BARRIER_SYNC_VERTEX_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        if (pNormBuf)
        {
            m_GpuTaskGraph.DeclareBufferUsage(drawTask, pNormBuf,
                D3D12_BARRIER_SYNC_VERTEX_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        }
        drawTask.RecordFunc = [posGpuAddr, baseGpuHandle, vertexCount, this](ID3D12GraphicsCommandList* pCL)
        {
            pCL->SetPipelineState(m_pDefaultPSO);
            pCL->SetGraphicsRootSignature(m_pDefaultRootSig);
            pCL->OMSetRenderTargets(3, m_GBufferRTVs, FALSE, &m_CurrentDSV);
            pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            pCL->SetDescriptorHeaps(2, m_DescriptorHeapsArray);
            pCL->SetGraphicsRootShaderResourceView(1, posGpuAddr);
            pCL->SetGraphicsRootDescriptorTable(3, baseGpuHandle);
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
Gem::Result CRenderQueue12::StageBufferUpload(
    const Canvas::GfxResourceAllocation& destination,
    const void* pData,
    uint64_t dataSize)
{
    if (!pData || dataSize == 0 || !destination.pBuffer)
        return Gem::Result::InvalidArg;

    try
    {
        HostWriteAllocation hw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(dataSize, hw));
        memcpy(hw.pMapped, pData, dataSize);

        m_PendingBufferUploads.push_back({ hw.pResource, hw.ResourceOffset, dataSize, destination });
    }
    catch (Gem::GemError& e) { return e.Result(); }
    catch (_com_error& e) { return ResultFromHRESULT(e.Error()); }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CRenderQueue12::DrawUIText(
    const Canvas::GfxResourceAllocation& vertexBuffer,
    Canvas::XGfxSurface* pGlyphAtlas,
    const Canvas::Math::FloatVector2& elementOffset)
{
    if (!pGlyphAtlas || !m_pCurrentSwapChain || !vertexBuffer.pBuffer)
        return Gem::Result::InvalidArg;

    try
    {
        DXGI_FORMAT rtvFormat = m_pCurrentSwapChain->m_pSurface->GetD3DResource()->GetDesc().Format;
        EnsureTextPSO(rtvFormat);

        auto pAtlas = static_cast<CSurface12*>(pGlyphAtlas);
        CSurface12* pBackBuffer = m_pCurrentSwapChain->m_pSurface;
        auto pVertexBuf = static_cast<CBuffer12*>(vertexBuffer.pBuffer.Get());
        uint32_t vertexCount = static_cast<uint32_t>(vertexBuffer.Size / sizeof(Canvas::TextVertex));

        // Allocate all CPU-side resources before creating the GPU task
        constexpr uint64_t kCBVSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        HostWriteAllocation hw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(kCBVSize, hw));

        float screenConsts[4] = { static_cast<float>(m_DepthBufferWidth), static_cast<float>(m_DepthBufferHeight), elementOffset.X, elementOffset.Y };
        memcpy(hw.pMapped, screenConsts, sizeof(screenConsts));

        ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();
        const UINT incSize = m_CbvSrvUavIncrement;
        if (m_NextSRVSlot + 1 > NumShaderResourceDescriptors)
            m_NextSRVSlot = 0;
        UINT srvSlot = m_NextSRVSlot++;

        CD3DX12_CPU_DESCRIPTOR_HANDLE srvCpuHandle(m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), srvSlot, incSize);
        CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), srvSlot, incSize);
        pD3DDevice->CreateShaderResourceView(pAtlas->GetD3DResource(), nullptr, srvCpuHandle);

        D3D12_GPU_VIRTUAL_ADDRESS cbvAddr = hw.GpuAddress;
        D3D12_GPU_VIRTUAL_ADDRESS vertexAddr = pVertexBuf->GetD3DResource()->GetGPUVirtualAddress() + vertexBuffer.Offset;

        // All resources ready — now create the GPU task
        auto& drawTask = m_UIGpuTaskGraph.CreateTask("DrawUIText");
        m_UIGpuTaskGraph.DeclareTextureUsage(drawTask, pAtlas,
            D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_PIXEL_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        m_UIGpuTaskGraph.DeclareTextureUsage(drawTask, pBackBuffer,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        m_UIGpuTaskGraph.DeclareBufferUsage(drawTask, pVertexBuf,
            D3D12_BARRIER_SYNC_PIXEL_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

        drawTask.RecordFunc = [cbvAddr, vertexAddr, srvGpuHandle, vertexCount, this](ID3D12GraphicsCommandList* pCL)
        {
            pCL->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, nullptr);
            pCL->SetDescriptorHeaps(2, m_DescriptorHeapsArray);
            pCL->SetGraphicsRootSignature(m_pTextRootSig);
            pCL->SetPipelineState(m_pTextPSO);
            pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            pCL->SetGraphicsRootConstantBufferView(0, cbvAddr);
            pCL->SetGraphicsRootDescriptorTable(2, srvGpuHandle);
            pCL->SetGraphicsRootShaderResourceView(1, vertexAddr);
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
void CRenderQueue12::FlushPendingBufferUploads()
{
    if (m_PendingBufferUploads.empty())
        return;

    auto& copyTask = m_UIGpuTaskGraph.CreateTask("FlushBufferUploads");

    std::unordered_set<CBuffer12*> declaredBuffers;
    for (const auto& u : m_PendingBufferUploads)
    {
        auto pBuf = static_cast<CBuffer12*>(u.Destination.pBuffer.Get());
        if (declaredBuffers.insert(pBuf).second)
        {
            m_UIGpuTaskGraph.DeclareBufferUsage(copyTask, pBuf,
                D3D12_BARRIER_SYNC_COPY,
                D3D12_BARRIER_ACCESS_COPY_DEST);
        }
    }

    // Lambda captures m_BufferCopyOpScratch by reference; RecordFunc runs
    // synchronously inside InsertTask so the scratch is still valid.
    m_BufferCopyOpScratch.clear();
    m_BufferCopyOpScratch.reserve(m_PendingBufferUploads.size());
    for (const auto& u : m_PendingBufferUploads)
    {
        m_BufferCopyOpScratch.push_back({
            u.pStagingResource,
            u.StagingOffset,
            static_cast<CBuffer12*>(u.Destination.pBuffer.Get())->GetD3DResource(),
            u.Destination.Offset,
            u.CopySize
        });
    }

    copyTask.RecordFunc = [&ops = m_BufferCopyOpScratch](ID3D12GraphicsCommandList* pCL)
    {
        for (const auto& op : ops)
            pCL->CopyBufferRegion(op.pDst, op.DstOffset, op.pSrc, op.SrcOffset, op.Size);
    };
    m_UIGpuTaskGraph.InsertTask(copyTask);

    m_PendingBufferUploads.clear();
}

//------------------------------------------------------------------------------------------------
void CRenderQueue12::FlushPendingGlyphUploads()
{
    auto* pCanvas = m_pDevice->GetCanvas();
    if (!pCanvas)
        return;

    auto* pCache = pCanvas->GetGlyphCache();
    if (!pCache || !pCache->HasPendingUploads())
        return;

    auto* pAtlas = m_pDevice->GetGlyphAtlasSurface();
    if (!pAtlas)
        return;

    const uint8_t* pStagingData = pCache->GetStagingData();
    auto uploads = pCache->TakePendingUploads();
    for (auto& upload : uploads)
    {
        Gem::ThrowGemError(UploadTextureRegion(
            pAtlas, upload.AtlasX, upload.AtlasY,
            upload.Width, upload.Height,
            pStagingData + upload.PixelOffset,
            upload.Width * upload.BytesPerPixel));
    }
    pCache->ClearStagingBuffer();
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
    const Canvas::GfxResourceAllocation& vertexBuffer,
    const Canvas::Math::FloatVector2& elementOffset)
{
    if (!m_pCurrentSwapChain || !vertexBuffer.pBuffer)
        return Gem::Result::InvalidArg;

    try
    {
        DXGI_FORMAT rtvFormat = m_pCurrentSwapChain->m_pSurface->GetD3DResource()->GetDesc().Format;
        EnsureRectPSO(rtvFormat);

        CSurface12* pBackBuffer = m_pCurrentSwapChain->m_pSurface;
        auto pVertexBuf = static_cast<CBuffer12*>(vertexBuffer.pBuffer.Get());
        uint32_t vertexCount = static_cast<uint32_t>(vertexBuffer.Size / sizeof(Canvas::TextVertex));

        // Allocate all CPU-side resources before creating the GPU task
        constexpr uint64_t kCBVSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        HostWriteAllocation hw;
        Gem::ThrowGemError(m_UploadRing.AllocateFromRing(kCBVSize, hw));

        float screenConsts[4] = { static_cast<float>(m_DepthBufferWidth), static_cast<float>(m_DepthBufferHeight), elementOffset.X, elementOffset.Y };
        memcpy(hw.pMapped, screenConsts, sizeof(screenConsts));

        D3D12_GPU_VIRTUAL_ADDRESS cbvAddr = hw.GpuAddress;
        D3D12_GPU_VIRTUAL_ADDRESS vertexAddr = pVertexBuf->GetD3DResource()->GetGPUVirtualAddress() + vertexBuffer.Offset;

        // All resources ready — now create the GPU task
        auto& drawTask = m_UIGpuTaskGraph.CreateTask("DrawUIRect");
        m_UIGpuTaskGraph.DeclareTextureUsage(drawTask, pBackBuffer,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET);
        m_UIGpuTaskGraph.DeclareBufferUsage(drawTask, pVertexBuf,
            D3D12_BARRIER_SYNC_PIXEL_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

        drawTask.RecordFunc = [cbvAddr, vertexAddr, vertexCount, this](ID3D12GraphicsCommandList* pCL)
        {
            pCL->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, nullptr);
            pCL->SetGraphicsRootSignature(m_pRectRootSig);
            pCL->SetPipelineState(m_pRectPSO);
            pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            pCL->SetGraphicsRootConstantBufferView(0, cbvAddr);
            pCL->SetGraphicsRootShaderResourceView(1, vertexAddr);
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
GEMMETHODIMP CRenderQueue12::SubmitForRender(Canvas::XSceneGraphNode *pNode)
{
    if (!pNode)
        return Gem::Result::InvalidArg;

    // Route lights directly to SubmitLight — they don't go in the renderable queue
    UINT elementCount = pNode->GetBoundElementCount();
    for (UINT i = 0; i < elementCount; ++i)
    {
        Gem::TGemPtr<Canvas::XLight> pLight;
        if (SUCCEEDED(pNode->GetBoundElement(i)->QueryInterface(&pLight)))
            Gem::ThrowGemError(SubmitLight(pLight));
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
Gem::Result CRenderQueue12::SubmitLight(Canvas::XLight *pLight)
{
    if (!pLight || m_LightCount >= MAX_LIGHTS_PER_REGION)
        return Gem::Result::Success;

    // Skip disabled lights
    if (!(pLight->GetFlags() & Canvas::LightFlags::Enabled))
        return Gem::Result::Success;

    HlslTypes::HlslLight& gpu = m_Lights[m_LightCount++];
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
        if (pLight->GetType() == Canvas::LightType::Directional || pLight->GetType() == Canvas::LightType::Spot)
        {
            Canvas::Math::FloatVector4 dir(world[0][0], world[0][1], world[0][2], 0.0f);
            dir = dir.Normalize();
            gpu.DirectionOrPosition = { dir[0], dir[1], dir[2], 0.0f };

            if (pLight->GetType() == Canvas::LightType::Spot)
            {
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
        }
        else
        {
            gpu.DirectionOrPosition = { world[3][0], world[3][1], world[3][2], 1.0f };
        }
    }
    else
    {
        if (pLight->GetType() == Canvas::LightType::Directional || pLight->GetType() == Canvas::LightType::Spot)
            gpu.DirectionOrPosition = { 0.0f, 1.0f, 0.0f, 0.0f };
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

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
GEMMETHODIMP CRenderQueue12::EndFrame()
{
    try
    {
        // Build per-frame constants from the active camera + accumulated lights
        // (lights were accumulated during SubmitForRender via SubmitLight)
        HlslTypes::HlslPerFrameConstants frameConstants = {};
        
        if (m_pActiveCamera)
        {
            auto viewProj = m_pActiveCamera->GetViewProjectionMatrix();
            memcpy(&frameConstants.ViewProj, &viewProj, sizeof(frameConstants.ViewProj));

            auto *pCameraNode = m_pActiveCamera->GetAttachedNode();
            if (pCameraNode)
            {
                auto camPos = pCameraNode->GetGlobalTranslation();
                memcpy(&frameConstants.CameraWorldPos, &camPos, sizeof(frameConstants.CameraWorldPos));
            }
        }

        frameConstants.LightCount = m_LightCount;
        frameConstants.LightCullThreshold = kDefaultLightCullThreshold;
        if (m_LightCount > 0)
            memcpy(frameConstants.Lights, m_Lights, m_LightCount * sizeof(HlslTypes::HlslLight));

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

        // Drain the renderable queue — process nodes inline
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
                        Gem::ThrowGemError(DrawMesh(pMeshData, pNode->GetGlobalMatrix()));
                    }
                }
                // Lights were already accumulated during SubmitForRender — skip here
            }
        }
        m_RenderableQueue.clear();

        //==========================================================================================
        // Composition pass: read G-buffers, perform deferred lighting, write to back buffer
        //==========================================================================================
        {
            CSurface12* pBackBuffer = m_pCurrentSwapChain->m_pSurface;

            // CPU work: allocate SRV descriptors and create SRVs
            ID3D12Device* pD3DDevice = m_pDevice->GetD3DDevice();
            const UINT incSize = m_CbvSrvUavIncrement;

            if (m_NextSRVSlot + 3 > NumShaderResourceDescriptors)
                m_NextSRVSlot = 0;
            UINT baseSlot = m_NextSRVSlot;
            m_NextSRVSlot += 3;

            CD3DX12_CPU_DESCRIPTOR_HANDLE baseCpuHandle(m_pShaderResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), baseSlot, incSize);
            CD3DX12_GPU_DESCRIPTOR_HANDLE baseGpuHandle(m_pShaderResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), baseSlot, incSize);

            pD3DDevice->CreateShaderResourceView(m_pGBufferNormals->GetD3DResource(), nullptr, baseCpuHandle);
            CD3DX12_CPU_DESCRIPTOR_HANDLE diffuseSrvHandle(baseCpuHandle, 1, incSize);
            pD3DDevice->CreateShaderResourceView(m_pGBufferDiffuseColor->GetD3DResource(), nullptr, diffuseSrvHandle);
            CD3DX12_CPU_DESCRIPTOR_HANDLE worldPosSrvHandle(baseCpuHandle, 2, incSize);
            pD3DDevice->CreateShaderResourceView(m_pGBufferWorldPos->GetD3DResource(), nullptr, worldPosSrvHandle);

            // Composite task: transition G-buffers to SR, back buffer to RT, then draw
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
            DeclareGpuTextureUsage(compositeTask, pBackBuffer,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            compositeTask.RecordFunc = [frameCBVAddress, baseGpuHandle, this](ID3D12GraphicsCommandList* pCL)
            {
                // BUGBUG: TODO: Make clearColor selectable by client application
                const float clearColor[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
                pCL->ClearRenderTargetView(m_CurrentRTV, clearColor, 0, nullptr);
                pCL->OMSetRenderTargets(1, &m_CurrentRTV, FALSE, nullptr);
                pCL->SetGraphicsRootSignature(m_pCompositeRootSig);
                pCL->SetPipelineState(m_pCompositePSO);
                pCL->SetDescriptorHeaps(2, m_DescriptorHeapsArray);
                pCL->SetGraphicsRootConstantBufferView(0, frameCBVAddress);
                pCL->SetGraphicsRootDescriptorTable(1, baseGpuHandle);
                pCL->DrawInstanced(3, 1, 0, 0);
            };
            m_GpuTaskGraph.InsertTask(compositeTask);
        }

        // Flush pending glyph atlas uploads (SDF bitmaps → GPU texture)
        FlushPendingGlyphUploads();

        // Flush pending vertex uploads staged during SubmitRenderables
        FlushPendingBufferUploads();

        // Draw UI elements (pure draw — uploads already staged by SubmitRenderables)
        for (auto* pNode : m_UIRenderableQueue)
        {
            UINT elemCount = pNode->GetBoundElementCount();
            for (UINT i = 0; i < elemCount; ++i)
            {
                auto* pElem = pNode->GetBoundElement(i);
                if (!pElem->IsVisible())
                    continue;
                auto& vb = pElem->GetVertexBuffer();
                if (vb.Size == 0)
                    continue;

                // Keep GPU resources alive until this frame's fence completes
                DeferRelease(vb.pBuffer.Get());

                Canvas::Math::FloatVector2 elementOffset = pNode->GetGlobalPosition();

                if (pElem->GetType() == Canvas::UIElementType::Rect)
                {
                    Gem::ThrowGemError(DrawUIRect(vb, elementOffset));
                }
                else if (pElem->GetType() == Canvas::UIElementType::Text)
                {
                    Gem::TGemPtr<Canvas::XUITextElement> pText;
                    if (SUCCEEDED(pElem->QueryInterface(&pText)))
                    {
                        auto* pAtlas = pText->GetAtlasSurface();
                        if (pAtlas)
                        {
                            DeferRelease(pAtlas);
                            Gem::ThrowGemError(DrawUIText(vb, pAtlas, elementOffset));
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
        m_pCurrentSwapChain = nullptr;
        m_pActiveCamera = nullptr;
        return e.Result();
    }

    m_pCurrentSwapChain = nullptr;
    m_pActiveCamera = nullptr;
    m_LightCount = 0;
    return Gem::Result::Success;
}
