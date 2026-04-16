//================================================================================================
// Device12
//================================================================================================

#include "pch.h"

#include "RenderQueue12.h"
#include "Surface12.h"
#include "Buffer12.h"
#include "MeshData12.h"
#include "GlyphAtlas.h"

//------------------------------------------------------------------------------------------------
CDevice12::CDevice12(Canvas::XCanvas* pCanvas, PCSTR name) :
    TGfxElement(pCanvas)
{
    if (name != nullptr)
        SetName(name);
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
    }
    catch (_com_error &e)
    {
        Canvas::LogError(GetLogger(), "CDevice12::Initialize: HRESULT 0x%08x", e.Error());
        return ResultFromHRESULT(e.Error());
    }

    m_ResourceAllocator.Initialize(this);

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
GEMMETHODIMP CDevice12::CreateMaterial()
{
    Canvas::CFunctionSentinel sentinel("XGfxDevice::CreateMaterial", GetLogger());
    
   return Gem::Result::NotImplemented;
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
void CDevice12::EnsureUploadRingBuffer()
{
    if (m_pUploadRingResource)
        return;

    D3D12_RESOURCE_DESC1 bufDesc = {};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = m_UploadRingSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    ThrowFailedHResult(m_pD3DDevice->CreateCommittedResource3(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, nullptr, 0, nullptr,
        IID_PPV_ARGS(&m_pUploadRingResource)));
    m_pUploadRingResource->SetName(L"CanvasGfx_UploadRing");

    // Persistently map — UPLOAD heap stays mapped for the lifetime of the resource
    void* pMapped = nullptr;
    ThrowFailedHResult(m_pUploadRingResource->Map(0, nullptr, &pMapped));
    m_pUploadRingMapped = static_cast<uint8_t*>(pMapped);
    m_UploadRingGpuBase = m_pUploadRingResource->GetGPUVirtualAddress();
}

//------------------------------------------------------------------------------------------------
Gem::Result CDevice12::AllocateFromRing(uint64_t sizeInBytes, HostWriteAllocation& out)
{
    if (sizeInBytes == 0)
        return Gem::Result::InvalidArg;

    try
    {
        constexpr uint64_t kAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        uint64_t alignedSize = (sizeInBytes + kAlignment - 1) & ~(kAlignment - 1);

        EnsureUploadRingBuffer();

        // Check available space
        uint64_t available;
        if (m_UploadRingWriteOffset >= m_UploadRingReadOffset)
            available = m_UploadRingSize - m_UploadRingWriteOffset + m_UploadRingReadOffset;
        else
            available = m_UploadRingReadOffset - m_UploadRingWriteOffset;

        // Try reclaiming completed frames
        if (alignedSize > available)
        {
            ReclaimUploadRingSpace(m_LastCompletedFenceValue);
            if (m_UploadRingWriteOffset >= m_UploadRingReadOffset)
                available = m_UploadRingSize - m_UploadRingWriteOffset + m_UploadRingReadOffset;
            else
                available = m_UploadRingReadOffset - m_UploadRingWriteOffset;
        }

        // Grow if needed
        if (alignedSize > available)
        {
            uint64_t needed = m_UploadRingSize;
            while (needed < alignedSize)
                needed *= 2;
            GrowUploadRingBuffer(needed * 2);
        }

        // Handle wrap-around
        if (m_UploadRingWriteOffset + alignedSize > m_UploadRingSize)
        {
            if (alignedSize > m_UploadRingReadOffset)
                GrowUploadRingBuffer(m_UploadRingSize * 2);
            else
                m_UploadRingWriteOffset = 0;
        }

        out.GpuAddress      = m_UploadRingGpuBase + m_UploadRingWriteOffset;
        out.pMapped         = m_pUploadRingMapped + m_UploadRingWriteOffset;
        out.Size            = sizeInBytes;
        out.pResource       = m_pUploadRingResource;
        out.ResourceOffset  = m_UploadRingWriteOffset;

        m_UploadRingWriteOffset += alignedSize;
        return Gem::Result::Success;
    }
    catch (const Gem::GemError &e) { return e.Result(); }
    catch (const _com_error &e) { return ResultFromHRESULT(e.Error()); }
}

//------------------------------------------------------------------------------------------------
void CDevice12::MarkUploadRingFrameEnd(UINT64 fenceValue)
{
    m_UploadRingFrameMarkers.push_back({ fenceValue, m_UploadRingWriteOffset });
}

//------------------------------------------------------------------------------------------------
void CDevice12::ReclaimUploadRingSpace(UINT64 completedFenceValue)
{
    m_LastCompletedFenceValue = completedFenceValue;
    while (!m_UploadRingFrameMarkers.empty())
    {
        auto& oldest = m_UploadRingFrameMarkers.front();
        if (oldest.FenceValue > completedFenceValue)
            break;
        m_UploadRingReadOffset = oldest.WriteOffset;
        m_UploadRingFrameMarkers.pop_front();
    }
}

//------------------------------------------------------------------------------------------------
void CDevice12::GrowUploadRingBuffer(uint64_t newSize)
{
    // Unmap and release old ring buffer
    if (m_pUploadRingResource)
    {
        m_pUploadRingResource->Unmap(0, nullptr);
        m_pUploadRingResource.Release();
        m_pUploadRingMapped = nullptr;
        m_UploadRingGpuBase = 0;
    }

    // Create new larger buffer
    m_UploadRingSize = newSize;
    m_UploadRingWriteOffset = 0;
    m_UploadRingReadOffset = 0;
    m_UploadRingFrameMarkers.clear();
    EnsureUploadRingBuffer();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateMeshData(
    [[maybe_unused]] uint32_t vertexCount,
    [[maybe_unused]] const Canvas::Math::FloatVector4 *positions,
    [[maybe_unused]] const Canvas::Math::FloatVector4 *normals,
    [[maybe_unused]] Canvas::XGfxRenderQueue *pRenderQueue,
    [[maybe_unused]] Canvas::XGfxMeshData **ppMesh,
    [[maybe_unused]] const char* name)
{
    // Two FloatVector4 arrays: positions and normals
    uint64_t posSize = static_cast<uint64_t>(vertexCount) * sizeof(Canvas::Math::FloatVector4);
    uint64_t normSize = posSize;
    uint64_t allocationSize = posSize + normSize;

    if (!ppMesh)
        return Gem::Result::BadPointer;

    *ppMesh = nullptr;

    try
    {
        // Allocate device-local buffers via resource allocator (placed in shared heaps)
        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Height           = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels        = 1;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        // Build debug names from mesh name
        std::string posName = name ? (std::string(name) + "_Positions") : "MeshPositions";
        std::string normName = name ? (std::string(name) + "_Normals") : "MeshNormals";

        bufDesc.Width = posSize;
        ResourceAllocation posAlloc;
        Gem::ThrowGemError(m_ResourceAllocator.Alloc(bufDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, posAlloc, posName.c_str()));

        bufDesc.Width = normSize;
        ResourceAllocation normAlloc;
        Gem::ThrowGemError(m_ResourceAllocator.Alloc(bufDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, normAlloc, normName.c_str()));

        // Wrap placed resources in CBuffer12
        Gem::TGemPtr<CBuffer12> pPosBuffer;
        Gem::ThrowGemError(TGfxElement<CBuffer12>::CreateAndRegister<CBuffer12>(
            &pPosBuffer, GetCanvas(), posAlloc.pResource, posName.c_str()));
        pPosBuffer->SetAllocationTracking(this, posAlloc.AllocationKey, posAlloc.SizeInUnits, posAlloc.AllocatorTier);

        Gem::TGemPtr<CBuffer12> pNormBuffer;
        Gem::ThrowGemError(TGfxElement<CBuffer12>::CreateAndRegister<CBuffer12>(
            &pNormBuffer, GetCanvas(), normAlloc.pResource, normName.c_str()));
        pNormBuffer->SetAllocationTracking(this, normAlloc.AllocationKey, normAlloc.SizeInUnits, normAlloc.AllocatorTier);

        // Allocate staging space from upload ring buffer
        HostWriteAllocation staging;
        Gem::ThrowGemError(AllocateFromRing(allocationSize, staging));

        // Copy data into the upload buffer immediately on CPU timeline
        {
            uint8_t* dst = static_cast<uint8_t*>(staging.pMapped);
            if (positions && posSize > 0)
                memcpy(dst, positions, static_cast<size_t>(posSize));
            if (normals && normSize > 0)
                memcpy(dst + posSize, normals, static_cast<size_t>(normSize));
        }

        // Schedule copy operations from upload heap to device-local heap
        if (pRenderQueue)
        {
            CRenderQueue12* pRQ = static_cast<CRenderQueue12*>(pRenderQueue);

            // Declare resource usage: destination buffers need barriers for copy
            ResourceUsageBuilder usages;
            usages.SetBufferUsage(
                static_cast<CBuffer12*>(pPosBuffer.Get()),
                D3D12_BARRIER_SYNC_COPY,
                D3D12_BARRIER_ACCESS_COPY_DEST);

            usages.SetBufferUsage(
                static_cast<CBuffer12*>(pNormBuffer.Get()),
                D3D12_BARRIER_SYNC_COPY,
                D3D12_BARRIER_ACCESS_COPY_DEST);

            ID3D12Resource* pSrcRes  = staging.pResource;
            ID3D12Resource* pDstPos  = pPosBuffer->GetD3DResource();
            ID3D12Resource* pDstNorm = pNormBuffer->GetD3DResource();
            uint64_t srcOffset = staging.ResourceOffset;

            pRQ->RecordCommandBlock(
                usages.Build(),
                [pSrcRes, pDstPos, pDstNorm, srcOffset, posSize, normSize](ID3D12GraphicsCommandList* cmdList)
                {
                    cmdList->CopyBufferRegion(pDstPos, 0, pSrcRes, srcOffset, posSize);
                    cmdList->CopyBufferRegion(pDstNorm, 0, pSrcRes, srcOffset + posSize, normSize);
                },
                "Upload Mesh Data");
        }

        // Create and register the CMeshData12 object that holds the buffers
        Gem::TGemPtr<CMeshData12> pMeshData;
        Gem::ThrowGemError(TGfxElement<Canvas::XGfxMeshData>::CreateAndRegister(&pMeshData, GetCanvas(), name));
        
        // Build GfxResourceAllocations for the mesh (CBuffer12 handles cleanup via allocator tracking)
        Canvas::GfxResourceAllocation posVB;
        posVB.pBuffer       = pPosBuffer;
        posVB.Offset        = 0;
        posVB.Size          = posSize;

        Canvas::GfxResourceAllocation normVB;
        normVB.pBuffer       = pNormBuffer;
        normVB.Offset        = 0;
        normVB.Size          = normSize;

        pMeshData->SetPositionBuffer(posVB);
        pMeshData->SetNormalBuffer(normVB);
        
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
    Canvas::XGfxRenderQueue *pRenderQueue,
    Canvas::XGfxMeshData **ppMesh,
    const char* name)
{
    return CreateMeshData(vertexCount, positions, normals, pRenderQueue, ppMesh, name);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::AllocVertexBuffer(uint32_t vertexCount, uint32_t vertexStride, const void* pVertexData, Canvas::XGfxRenderQueue* pRQ, Canvas::GfxResourceAllocation& inOut)
{
    if (vertexCount == 0 || vertexStride == 0 || !pVertexData || !pRQ)
        return Gem::Result::InvalidArg;

    try
    {
        auto* pRQ12 = static_cast<CRenderQueue12*>(pRQ);

        // Retire the previous buffer (if any) so the GPU can finish using it
        if (inOut.pBuffer)
            pRQ12->RetireBuffer(inOut.pBuffer, pRQ12->GetCurrentFenceValue() + 1);

        uint64_t dataSize = static_cast<uint64_t>(vertexCount) * vertexStride;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width            = dataSize;
        bufferDesc.Height           = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels        = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ResourceAllocation alloc;
        Gem::ThrowGemError(m_ResourceAllocator.Alloc(bufferDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, alloc));

        // Wrap placed/committed resource in CBuffer12
        Gem::TGemPtr<CBuffer12> pBuffer;
        Gem::ThrowGemError(TGfxElement<CBuffer12>::CreateAndRegister<CBuffer12>(
            &pBuffer, GetCanvas(), alloc.pResource, "VertexBuffer"));
        pBuffer->SetAllocationTracking(this, alloc.AllocationKey, alloc.SizeInUnits, alloc.AllocatorTier);

        inOut.pBuffer       = pBuffer;
        inOut.Offset        = 0;
        inOut.Size          = dataSize;

        // Stage the upload via the render queue
        Gem::ThrowGemError(pRQ12->StageBufferUpload(inOut, pVertexData, dataSize));

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
    uint32_t srcRowPitch,
    Canvas::XGfxRenderQueue *pRenderQueue)
{
    if (!pDstSurface || !pData || !pRenderQueue || width == 0 || height == 0)
        return Gem::Result::BadPointer;

    auto* pRQ = static_cast<CRenderQueue12*>(pRenderQueue);
    return pRQ->UploadTextureRegion(pDstSurface, dstX, dstY, width, height, pData, srcRowPitch,
        Canvas::GfxRenderContext::UI);
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
