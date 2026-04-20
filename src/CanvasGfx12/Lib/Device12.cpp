//================================================================================================
// Device12
//================================================================================================

#include "pch.h"

#include "RenderQueue12.h"
#include "Surface12.h"
#include "Buffer12.h"
#include "MeshData12.h"
#include "Material12.h"
#include "GlyphAtlas.h"

//------------------------------------------------------------------------------------------------
#if defined(_DEBUG)
static void __stdcall D3D12DebugMessageCallback(
    D3D12_MESSAGE_CATEGORY /*Category*/,
    D3D12_MESSAGE_SEVERITY Severity,
    D3D12_MESSAGE_ID ID,
    LPCSTR pDescription,
    void* pContext)
{
    try
    {
        auto* pLogger = static_cast<Canvas::XLogger*>(pContext);

        switch (Severity)
        {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
            Canvas::LogCritical(pLogger, "[D3D12 %u] %s", static_cast<unsigned>(ID), pDescription);
            break;
        case D3D12_MESSAGE_SEVERITY_ERROR:
            Canvas::LogError(pLogger, "[D3D12 %u] %s", static_cast<unsigned>(ID), pDescription);
            break;
        case D3D12_MESSAGE_SEVERITY_WARNING:
            Canvas::LogWarn(pLogger, "[D3D12 %u] %s", static_cast<unsigned>(ID), pDescription);
            break;
        case D3D12_MESSAGE_SEVERITY_INFO:
            Canvas::LogInfo(pLogger, "[D3D12 %u] %s", static_cast<unsigned>(ID), pDescription);
            break;
        case D3D12_MESSAGE_SEVERITY_MESSAGE:
        default:
            Canvas::LogDebug(pLogger, "[D3D12 %u] %s", static_cast<unsigned>(ID), pDescription);
            break;
        }
    }
    catch (...)
    {
        // Never let exceptions escape across the D3D runtime boundary.
    }
}
#endif

//------------------------------------------------------------------------------------------------
CDevice12::CDevice12(Canvas::XCanvas* pCanvas, PCSTR name) :
    TGfxElement(pCanvas)
{
    if (name != nullptr)
        SetName(name);
}

//------------------------------------------------------------------------------------------------
CDevice12::~CDevice12()
{
#if defined(_DEBUG)
    // Unregister the debug callback first — the spec guarantees this will not
    // return until any outstanding callbacks have completed, so it is safe to
    // release the cached logger immediately afterwards.
    if (m_pInfoQueue1 && m_debugCallbackCookie != 0)
    {
        m_pInfoQueue1->UnregisterMessageCallback(m_debugCallbackCookie);
        m_debugCallbackCookie = 0;
    }
    m_pInfoQueue1.Release();
    m_pDebugLogger = nullptr;
#endif

    // Drain the copy queue and unregister its timeline before tearing down the
    // resource manager — the copy queue defers releases through the manager's
    // per-timeline queues which Shutdown() will then drain.
    m_CopyQueue.Shutdown();

    // Drain the resource manager before members are destroyed, breaking the
    // CDevice12 -> manager -> CBuffer12 -> CDevice12 reference cycle.
    m_ResourceManager.Shutdown();
}

//------------------------------------------------------------------------------------------------
Gem::Result CDevice12::Initialize()
{
    try
    {
        // Create the device
        CComPtr<ID3D12Device10> pDevice;
        ThrowFailedHResult(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice)));
        m_pD3DDevice.Attach(pDevice.Detach());

        // Log the GPU device name and adapter information
        LUID adapterLuid = m_pD3DDevice->GetAdapterLuid();
        CComPtr<IDXGIFactory4> pFactory;
        CComPtr<IDXGIAdapter1> pAdapter;
        ThrowFailedHResult(CreateDXGIFactory2(0, IID_PPV_ARGS(&pFactory)));
        ThrowFailedHResult(pFactory->EnumAdapterByLuid(adapterLuid, IID_PPV_ARGS(&pAdapter)));
        
        DXGI_ADAPTER_DESC1 adapterDesc;
        ThrowFailedHResult(pAdapter->GetDesc1(&adapterDesc));
        
        // Get driver version using CheckInterfaceSupport
        LARGE_INTEGER driverVersion = {};
        HRESULT hr = pAdapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion);
        
        char deviceName[256];
        WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, deviceName, sizeof(deviceName), nullptr, nullptr);
        
        if (SUCCEEDED(hr))
        {
            // Extract driver version components from the LARGE_INTEGER (Product.Version.SubVersion.Build)
            UINT driverProduct = HIWORD(driverVersion.HighPart);
            UINT driverVersionNum = LOWORD(driverVersion.HighPart);
            UINT driverSubVersion = HIWORD(driverVersion.LowPart);
            UINT driverBuild = LOWORD(driverVersion.LowPart);
            
            Canvas::LogInfo(GetLogger(), "D3D12 Device created: %s (VendorId: 0x%04X, DeviceId: 0x%04X, Driver: %u.%u.%u.%u)", 
                            deviceName, adapterDesc.VendorId, adapterDesc.DeviceId,
                            driverProduct, driverVersionNum, driverSubVersion, driverBuild);
        }
        else
        {
            Canvas::LogInfo(GetLogger(), "D3D12 Device created: %s (VendorId: 0x%04X, DeviceId: 0x%04X)", 
                            deviceName, adapterDesc.VendorId, adapterDesc.DeviceId);
        }

#if defined(_DEBUG)
        // Register a debug layer message callback so D3D12 validation messages
        // are routed through the Canvas logger. This is best-effort — older
        // runtimes that lack ID3D12InfoQueue1 will simply skip registration.
        CComPtr<ID3D12InfoQueue1> pInfoQueue1;
        if (SUCCEEDED(m_pD3DDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue1))))
        {
            m_pDebugLogger = GetLogger();
            DWORD cookie = 0;
            if (m_pDebugLogger && SUCCEEDED(pInfoQueue1->RegisterMessageCallback(
                D3D12DebugMessageCallback,
                D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,
                static_cast<Canvas::XLogger*>(m_pDebugLogger),
                &cookie)))
            {
                m_pInfoQueue1 = pInfoQueue1;
                m_debugCallbackCookie = cookie;
                Canvas::LogInfo(GetLogger(), "D3D12 debug message callback registered");
            }
            else
            {
                m_pDebugLogger = nullptr;
            }
        }
#endif
    }
    catch (_com_error &e)
    {
        Canvas::LogError(GetLogger(), "CDevice12::Initialize: HRESULT 0x%08x", e.Error());
        return ResultFromHRESULT(e.Error());
    }

    m_ResourceManager.Initialize(this);
    m_CopyQueue.Initialize(this);

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateRenderQueue(Canvas::XGfxRenderQueue **ppRenderQueue)
{
    Canvas::CFunctionSentinel sentinel("XGfxDevice::CreateRenderQueue", GetLogger());
    try
    {
        // Create and register the CRenderQueue12
        Gem::TGemPtr<CRenderQueue12> pRenderQueue;
        Gem::ThrowGemError(TGfxElement<CRenderQueue12>::CreateAndRegister(&pRenderQueue, GetCanvas(), this, "RenderQueue"));
        
        return pRenderQueue->QueryInterface(ppRenderQueue);
    }
    catch (const Gem::GemError &e)
    {
        sentinel.SetResultCode(e.Result());
        return e.Result();
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateMaterial(Canvas::XGfxMaterial **ppMaterial)
{
    Canvas::CFunctionSentinel sentinel("XGfxDevice::CreateMaterial", GetLogger());

    if (!ppMaterial)
        return Gem::Result::BadPointer;

    *ppMaterial = nullptr;

    try
    {
        Gem::TGemPtr<CMaterial12> pMaterial;
        Gem::ThrowGemError(TGfxElement<Canvas::XGfxMaterial>::CreateAndRegister<CMaterial12>(
            &pMaterial, GetCanvas(), nullptr));
        *ppMaterial = pMaterial.Detach();
        return Gem::Result::Success;
    }
    catch (const Gem::GemError &e)
    {
        sentinel.SetResultCode(e.Result());
        return e.Result();
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateSurface(const Canvas::GfxSurfaceDesc &desc, Canvas::XGfxSurface **ppSurface)
{
    Canvas::CFunctionSentinel sentinel("XGfxDevice::CreateSurface", GetLogger());
    
    if (!ppSurface)
        return Gem::Result::BadPointer;
    
    *ppSurface = nullptr;
    
    try
    {
        // Convert GfxSurfaceDesc to D3D12_RESOURCE_DESC1
        D3D12_RESOURCE_DESC1 resourceDesc = {};
        
        switch (desc.Dimension)
        {
        case Canvas::GfxSurfaceDimension::Dimension1D:
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
            resourceDesc.Width = desc.Width;
            resourceDesc.Height = 1;
            resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.ArraySize);
            break;
        case Canvas::GfxSurfaceDimension::Dimension2D:
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            resourceDesc.Width = desc.Width;
            resourceDesc.Height = desc.Height;
            resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.ArraySize);
            break;
        case Canvas::GfxSurfaceDimension::Dimension3D:
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            resourceDesc.Width = desc.Width;
            resourceDesc.Height = desc.Height;
            resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.Depth);
            break;
        case Canvas::GfxSurfaceDimension::DimensionCube:
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            resourceDesc.Width = desc.Width;
            resourceDesc.Height = desc.Height;
            resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.ArraySize * 6);
            break;
        }
        
        resourceDesc.MipLevels = static_cast<UINT16>(desc.MipLevels);
        resourceDesc.Format = CanvasFormatToDXGIFormat(desc.Format);
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        
        // Set resource flags based on GfxSurfaceFlags
        if (desc.Flags & Canvas::SurfaceFlag_RenderTarget)
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (desc.Flags & Canvas::SurfaceFlag_DepthStencil)
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (desc.Flags & Canvas::SurfaceFlag_UnorderedAccess)
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        
        // Determine heap properties and initial state
        D3D12_HEAP_PROPERTIES heapProps = {};
        D3D12_BARRIER_LAYOUT initialLayout;
        
        if (desc.Flags & Canvas::SurfaceFlag_CpuUpload)
        {
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
            initialLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;
        }
        else if (desc.Flags & Canvas::SurfaceFlag_CpuReadback)
        {
            heapProps.Type = D3D12_HEAP_TYPE_READBACK;
            initialLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;
        }
        else
        {
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
            initialLayout = D3D12_BARRIER_LAYOUT_COMMON;
        }
        
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;
        
        // Provide optimized clear value for render target and depth-stencil surfaces
        D3D12_CLEAR_VALUE clearValue = {};
        const D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr;
        
        if (desc.Flags & Canvas::SurfaceFlag_DepthStencil)
        {
            clearValue.Format = resourceDesc.Format;
            clearValue.DepthStencil.Depth = 0.0f;  // Reverse-Z: far plane
            clearValue.DepthStencil.Stencil = 0;
            pOptimizedClearValue = &clearValue;
        }
        else if (desc.Flags & Canvas::SurfaceFlag_RenderTarget)
        {
            clearValue.Format = resourceDesc.Format;
            clearValue.Color[0] = 0.0f;
            clearValue.Color[1] = 0.0f;
            clearValue.Color[2] = 0.0f;
            clearValue.Color[3] = 0.0f;
            pOptimizedClearValue = &clearValue;
        }
        
        // Create the resource
        CComPtr<ID3D12Resource> pResource;
        ThrowFailedHResult(m_pD3DDevice->CreateCommittedResource3(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            initialLayout,
            pOptimizedClearValue,
            nullptr,
            0,
            nullptr,
            IID_PPV_ARGS(&pResource)));
        
        // Create and register the CSurface12 wrapper
        Gem::TGemPtr<CSurface12> pSurface;
        Gem::ThrowGemError(TGfxElement<CSurface12>::CreateAndRegister<CSurface12>(&pSurface, GetCanvas(), pResource, initialLayout, "Surface"));
        
        return pSurface->QueryInterface(ppSurface);
    }
    catch (const _com_error &e)
    {
        Canvas::LogError(GetLogger(), "CDevice12::CreateSurface: HRESULT 0x%08x", e.Error());
        sentinel.SetResultCode(ResultFromHRESULT(e.Error()));
        return ResultFromHRESULT(e.Error());
    }
    catch (const Gem::GemError &e)
    {
        sentinel.SetResultCode(e.Result());
        return e.Result();
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateBuffer(uint64_t sizeInBytes, Canvas::GfxMemoryUsage memoryUsage, Canvas::XGfxBuffer **ppBuffer)
{
    Canvas::CFunctionSentinel sentinel("XGfxDevice::CreateBuffer", GetLogger());
    
    if (!ppBuffer)
        return Gem::Result::BadPointer;
    
    *ppBuffer = nullptr;
    
    try
    {
        // Create buffer resource descriptor (D3D12_RESOURCE_DESC1 for Enhanced Barriers)
        D3D12_RESOURCE_DESC1 bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Alignment = 0;
        bufferDesc.Width = sizeInBytes;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.SampleDesc.Quality = 0;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        
        // Create buffer with Enhanced Barriers API
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = GfxMemoryUsageToD3D12HeapType(memoryUsage);
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;
        
        // Buffers don't have layouts in Enhanced Barriers - use UNDEFINED
        D3D12_BARRIER_LAYOUT initialLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;
        
        // Create the resource
        CComPtr<ID3D12Resource> pResource;
        ThrowFailedHResult(m_pD3DDevice->CreateCommittedResource3(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            initialLayout,
            nullptr,
            nullptr,
            0,
            nullptr,
            IID_PPV_ARGS(&pResource)));
        
        // Create and register the CBuffer12 wrapper
        const char* bufferName = (memoryUsage == Canvas::GfxMemoryUsage::HostWrite) ? "CanvasGfx_UploadBuffer"
                               : (memoryUsage == Canvas::GfxMemoryUsage::DeviceLocal) ? "CanvasGfx_DeviceBuffer"
                               : "CanvasGfx_Buffer";
        Gem::TGemPtr<CBuffer12> pBuffer;
        Gem::ThrowGemError(TGfxElement<CBuffer12>::CreateAndRegister<CBuffer12>(&pBuffer, GetCanvas(), pResource, bufferName));
        
        return pBuffer->QueryInterface(ppBuffer);
    }
    catch (const _com_error &e)
    {
        Canvas::LogError(GetLogger(), "CDevice12::CreateBuffer: HRESULT 0x%08x", e.Error());
        sentinel.SetResultCode(ResultFromHRESULT(e.Error()));
        return ResultFromHRESULT(e.Error());
    }
    catch (const Gem::GemError &e)
    {
        sentinel.SetResultCode(e.Result());
        return e.Result();
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateMeshData(
    const Canvas::MeshDataDesc &desc,
    Canvas::XGfxMeshData **ppMesh)
{
    if (!ppMesh)
        return Gem::Result::BadPointer;

    *ppMesh = nullptr;

    if (desc.GroupCount == 0 || desc.pGroups == nullptr)
        return Gem::Result::InvalidArg;

    // Validate every group up front: positions and normals are required, and
    // every present stream must have VertexCount entries (no count != 0 with
    // null pointers).
    for (uint32_t gi = 0; gi < desc.GroupCount; ++gi)
    {
        const Canvas::MeshDataGroupDesc &g = desc.pGroups[gi];
        if (g.VertexCount == 0)
            return Gem::Result::InvalidArg;
        if (g.pPositions == nullptr || g.pNormals == nullptr)
            return Gem::Result::InvalidArg;
    }

    const char *baseName = desc.pName;

    try
    {
        // Pre-compute a single staging allocation that holds every stream of
        // every group concatenated, so the upload ring is amortized across the
        // whole mesh.
        struct StreamLayout
        {
            uint32_t                                Group;
            Canvas::GfxVertexBufferType             Type;
            const void                             *pSrc;
            uint64_t                                Size;
            uint64_t                                StagingOffset;
        };

        std::vector<StreamLayout> streams;
        streams.reserve(desc.GroupCount * 4);

        uint64_t stagingTotal = 0;
        for (uint32_t gi = 0; gi < desc.GroupCount; ++gi)
        {
            const Canvas::MeshDataGroupDesc &g = desc.pGroups[gi];
            const uint64_t v4Size = static_cast<uint64_t>(g.VertexCount) * sizeof(Canvas::Math::FloatVector4);
            const uint64_t v2Size = static_cast<uint64_t>(g.VertexCount) * sizeof(Canvas::Math::FloatVector2);

            streams.push_back({ gi, Canvas::GfxVertexBufferType::Position, g.pPositions, v4Size, stagingTotal });
            stagingTotal += v4Size;
            streams.push_back({ gi, Canvas::GfxVertexBufferType::Normal,   g.pNormals,   v4Size, stagingTotal });
            stagingTotal += v4Size;

            if (g.pUV0)
            {
                streams.push_back({ gi, Canvas::GfxVertexBufferType::UV0, g.pUV0, v2Size, stagingTotal });
                stagingTotal += v2Size;
            }
            if (g.pTangents)
            {
                streams.push_back({ gi, Canvas::GfxVertexBufferType::Tangent, g.pTangents, v4Size, stagingTotal });
                stagingTotal += v4Size;
            }
        }

        HostWriteAllocation staging;
        Gem::ThrowGemError(m_CopyQueue.GetUploadRing().AllocateFromRing(stagingTotal, staging));
        {
            uint8_t *dst = static_cast<uint8_t *>(staging.pMapped);
            for (const StreamLayout &s : streams)
                memcpy(dst + s.StagingOffset, s.pSrc, static_cast<size_t>(s.Size));
        }

        // Allocate one D3D12 buffer per stream and enqueue its copy.
        std::vector<CMeshData12::GroupResources> groups(desc.GroupCount);

        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Height           = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels        = 1;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        auto streamSuffix = [](Canvas::GfxVertexBufferType t) -> const char *
        {
            switch (t)
            {
            case Canvas::GfxVertexBufferType::Position: return "_Positions";
            case Canvas::GfxVertexBufferType::Normal:   return "_Normals";
            case Canvas::GfxVertexBufferType::UV0:      return "_UV0";
            case Canvas::GfxVertexBufferType::Tangent:  return "_Tangents";
            default:                                    return "_Stream";
            }
        };

        for (const StreamLayout &s : streams)
        {
            std::string streamName = baseName ? std::string(baseName) : std::string("Mesh");
            streamName += streamSuffix(s.Type);
            if (desc.GroupCount > 1)
            {
                streamName += "_g";
                streamName += std::to_string(s.Group);
            }

            bufDesc.Width = s.Size;
            ResourceAllocation alloc;
            Gem::ThrowGemError(m_ResourceManager.Alloc(bufDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, alloc, streamName.c_str()));

            Gem::TGemPtr<CBuffer12> pBuffer;
            Gem::ThrowGemError(TGfxElement<CBuffer12>::CreateAndRegister<CBuffer12>(
                &pBuffer, GetCanvas(), alloc.pResource, streamName.c_str()));
            pBuffer->SetAllocationTracking(this, alloc.AllocationKey, alloc.SizeInUnits, alloc.AllocatorTier);

            Gem::TGemPtr<Gem::XGeneric> pKeepAlive;
            pBuffer->QueryInterface(&pKeepAlive);
            m_CopyQueue.EnqueueBufferCopy(
                staging.pResource, staging.ResourceOffset + s.StagingOffset,
                pBuffer->GetD3DResource(), 0,
                s.Size,
                std::move(pKeepAlive));

            Canvas::GfxResourceAllocation vb;
            vb.pBuffer = pBuffer;
            vb.Offset  = 0;
            vb.Size    = s.Size;

            CMeshData12::GroupResources &group = groups[s.Group];
            switch (s.Type)
            {
            case Canvas::GfxVertexBufferType::Position: group.PositionVB = vb; break;
            case Canvas::GfxVertexBufferType::Normal:   group.NormalVB   = vb; break;
            case Canvas::GfxVertexBufferType::UV0:      group.UV0VB      = vb; break;
            case Canvas::GfxVertexBufferType::Tangent:  group.TangentVB  = vb; break;
            default: break;
            }
        }

        // Attach materials (if any) to their groups.
        for (uint32_t gi = 0; gi < desc.GroupCount; ++gi)
            groups[gi].pMaterial = desc.pGroups[gi].pMaterial;

        Gem::TGemPtr<CMeshData12> pMeshData;
        Gem::ThrowGemError(TGfxElement<Canvas::XGfxMeshData>::CreateAndRegister(&pMeshData, GetCanvas(), baseName));
        pMeshData->SetGroups(std::move(groups));

        *ppMesh = pMeshData.Detach();
        return Gem::Result::Success;
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateDebugMeshData(
    uint32_t vertexCount,
    const Canvas::Math::FloatVector4 *positions,
    const Canvas::Math::FloatVector4 *normals,
    Canvas::XGfxMeshData **ppMesh,
    const char* name)
{
    return CreateMeshData(vertexCount, positions, normals, ppMesh, name);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::FlushUploads()
{
    m_CopyQueue.FlushIfPending();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::AllocVertexBuffer(uint32_t vertexCount, uint32_t vertexStride, const void* pVertexData, Canvas::XGfxRenderQueue* pRQ, Canvas::GfxResourceAllocation& inOut)
{
    if (vertexCount == 0 || vertexStride == 0 || !pVertexData || !pRQ)
        return Gem::Result::InvalidArg;

    try
    {
        uint64_t dataSize = static_cast<uint64_t>(vertexCount) * vertexStride;
        auto* pRQ12 = static_cast<CRenderQueue12*>(pRQ);

        // Caller is swapping in a fresh buffer; retire the old one to the pool.
        if (inOut.pBuffer)
            m_ResourceManager.RetireBuffer(std::move(inOut), pRQ12->MakeFenceToken());

        // Try to reuse a pooled buffer.
        if (!m_ResourceManager.AcquireBuffer(dataSize, inOut))
        {
            // Pool miss — allocate a new buffer.  Use power-of-2 capacity for
            // poolable sizes so the buffer fits a bucket exactly when retired.
            uint64_t capacity = dataSize;
            if (CResourceManager::IsPoolableSize(dataSize))
                capacity = CResourceManager::RoundUpPow2(dataSize);

            D3D12_RESOURCE_DESC bufferDesc = {};
            bufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufferDesc.Width            = capacity;
            bufferDesc.Height           = 1;
            bufferDesc.DepthOrArraySize = 1;
            bufferDesc.MipLevels        = 1;
            bufferDesc.SampleDesc.Count = 1;
            bufferDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ResourceAllocation alloc;
            Gem::ThrowGemError(m_ResourceManager.Alloc(bufferDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, alloc));

            Gem::TGemPtr<CBuffer12> pBuffer;
            Gem::ThrowGemError(TGfxElement<CBuffer12>::CreateAndRegister<CBuffer12>(
                &pBuffer, GetCanvas(), alloc.pResource, "VertexBuffer"));
            pBuffer->SetAllocationTracking(this, alloc.AllocationKey, alloc.SizeInUnits, alloc.AllocatorTier);

            inOut.pBuffer       = pBuffer;
        }

        inOut.Offset = 0;
        inOut.Size   = dataSize;

        // Stage and enqueue the upload on the device's copy queue.  The
        // consuming render queue gates on the copy fence at submit time.
        auto* pDstBuffer = static_cast<CBuffer12*>(inOut.pBuffer.Get());
        HostWriteAllocation staging;
        Gem::ThrowGemError(m_CopyQueue.GetUploadRing().AllocateFromRing(dataSize, staging));
        memcpy(staging.pMapped, pVertexData, static_cast<size_t>(dataSize));

        Gem::TGemPtr<Gem::XGeneric> pDstKeepAlive;
        pDstBuffer->QueryInterface(&pDstKeepAlive);
        m_CopyQueue.EnqueueBufferCopy(
            staging.pResource, staging.ResourceOffset,
            pDstBuffer->GetD3DResource(), 0,
            dataSize,
            std::move(pDstKeepAlive));

        return Gem::Result::Success;
    }
    catch (Gem::GemError& e) { return e.Result(); }
    catch (_com_error& e) { return ResultFromHRESULT(e.Error()); }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::UploadTextureRegion(
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
        auto* pDst = static_cast<CSurface12*>(pDstSurface);
        ID3D12Resource* pDstResource = pDst->GetD3DResource();

        constexpr uint32_t kPitchAlign = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        const uint32_t alignedRowPitch = (srcRowPitch + kPitchAlign - 1) & ~(kPitchAlign - 1);
        const uint64_t imageSize       = static_cast<uint64_t>(alignedRowPitch) * height;

        HostWriteAllocation hw;
        Gem::ThrowGemError(m_CopyQueue.GetUploadRing().AllocateFromRing(imageSize, hw));

        uint8_t* pStagingBase = static_cast<uint8_t*>(hw.pMapped);

        if (srcRowPitch == alignedRowPitch)
        {
            memcpy(pStagingBase, pData, static_cast<size_t>(srcRowPitch) * height);
        }
        else
        {
            for (uint32_t row = 0; row < height; ++row)
            {
                const uint8_t* pSrcRow = static_cast<const uint8_t*>(pData) + row * srcRowPitch;
                uint8_t*       pDstRow = pStagingBase + row * alignedRowPitch;
                memcpy(pDstRow, pSrcRow, srcRowPitch);
            }
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        footprint.Offset             = hw.ResourceOffset;
        footprint.Footprint.Format   = pDst->m_Desc.Format;
        footprint.Footprint.Width    = width;
        footprint.Footprint.Height   = height;
        footprint.Footprint.Depth    = 1;
        footprint.Footprint.RowPitch = alignedRowPitch;

        Gem::TGemPtr<Gem::XGeneric> pDstKeepAlive;
        pDst->QueryInterface(&pDstKeepAlive);
        m_CopyQueue.EnqueueTextureCopy(
            hw.pResource, footprint,
            pDstResource, /*dstSubresource*/ 0,
            dstX, dstY,
            width, height,
            std::move(pDstKeepAlive));

        return Gem::Result::Success;
    }
    catch (Gem::GemError& e) { return e.Result(); }
    catch (_com_error& e)    { return ResultFromHRESULT(e.Error()); }
}

//------------------------------------------------------------------------------------------------
Canvas::XGfxSurface* CDevice12::GetGlyphAtlasSurface()
{
    if (!m_pGlyphAtlasSurface)
    {
        auto* pCanvas = GetCanvas();
        if (!pCanvas)
            return nullptr;

        auto* pCache = pCanvas->GetGlyphCache();
        if (!pCache)
            return nullptr;

        Canvas::GfxSurfaceDesc desc = Canvas::GfxSurfaceDesc::SurfaceDesc2D(
            Canvas::GfxFormat::R8_UNorm,
            pCache->GetAtlasSize(), pCache->GetAtlasSize(),
            Canvas::SurfaceFlag_ShaderResource, 1);
        CreateSurface(desc, &m_pGlyphAtlasSurface);
    }
    return m_pGlyphAtlasSurface.Get();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateTextElement(Canvas::XUITextElement **ppElement)
{
    if (!ppElement)
        return Gem::Result::BadPointer;

    auto* pCanvas = GetCanvas();
    if (!pCanvas)
        return Gem::Result::NotFound;

    auto* pAtlas = GetGlyphAtlasSurface();
    return pCanvas->CreateTextElement(pAtlas, ppElement);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateRectElement(Canvas::XUIRectElement **ppElement)
{
    if (!ppElement)
        return Gem::Result::BadPointer;

    auto* pCanvas = GetCanvas();
    if (!pCanvas)
        return Gem::Result::NotFound;

    return pCanvas->CreateRectElement(ppElement);
}
