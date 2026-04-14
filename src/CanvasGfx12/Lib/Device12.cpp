//================================================================================================
// Device12
//================================================================================================

#include "pch.h"

#include "RenderQueue12.h"
#include "Surface12.h"
#include "Buffer12.h"
#include "MeshData12.h"

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
        Gem::ThrowGemError(TGfxElement<CRenderQueue12>::CreateAndRegister(&pRenderQueue, GetCanvas(), this, nullptr));
        
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
        Gem::ThrowGemError(TGfxElement<CSurface12>::CreateAndRegister<CSurface12>(&pSurface, GetCanvas(), pResource, initialLayout, nullptr));
        
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
        
        // Create and register the CBuffer12 wrapper (pass COMMON for legacy state tracking)
        Gem::TGemPtr<CBuffer12> pBuffer;
        Gem::ThrowGemError(TGfxElement<CBuffer12>::CreateAndRegister<CBuffer12>(&pBuffer, GetCanvas(), pResource, nullptr));
        
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
GEMMETHODIMP CDevice12::AllocateHostWriteRegion(uint64_t sizeInBytes, Canvas::GfxResourceAllocation &suballocation)
{
    Canvas::CFunctionSentinel sentinel("XGfxDevice::AllocateHostWriteRegion", GetLogger(), Canvas::LogLevel::Debug);

    if (sizeInBytes == 0)
        return Gem::Result::InvalidArg;

    try
    {
        // Round up to next power of 2 (min kMinBucketSize)
        uint64_t allocSize = uint64_t(1) << Log2Ceil(sizeInBytes > kMinBucketSize ? sizeInBytes : kMinBucketSize);

        Gem::TGemPtr<Canvas::XGfxBuffer> pBuffer;

        if (allocSize > kMaxBucketSize)
        {
            // Oversized: dedicated unpooled buffer
            Gem::ThrowGemError(CreateBuffer(allocSize, Canvas::GfxMemoryUsage::HostWrite, &pBuffer));
            suballocation.pBuffer = pBuffer;
            suballocation.Offset = 0;
            suballocation.Size = sizeInBytes;
            suballocation.AllocationKey = 0;  // 0 = unpooled
        }
        else
        {
            // Compute bucket index from log2(allocSize)
            uint32_t bucketIndex = Log2Ceil(allocSize) - kMinBucketLog2;

            auto& bucket = m_HostWriteBuckets[bucketIndex];
            if (!bucket.empty())
            {
                pBuffer = std::move(bucket.back());
                bucket.pop_back();
            }
            else
            {
                Gem::ThrowGemError(CreateBuffer(allocSize, Canvas::GfxMemoryUsage::HostWrite, &pBuffer));
            }

            suballocation.pBuffer = pBuffer;
            suballocation.Offset = 0;
            suballocation.Size = sizeInBytes;
            suballocation.AllocationKey = bucketIndex + 1;  // 1-based bucket index
        }

        return Gem::Result::Success;
    }
    catch (const Gem::GemError &e)
    {
        sentinel.SetResultCode(e.Result());
        return e.Result();
    }
    catch (const _com_error &e) { return ResultFromHRESULT(e.Error()); }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CDevice12::FreeHostWriteRegion(Canvas::GfxResourceAllocation &suballocation)
{
    Canvas::CFunctionSentinel sentinel("XGfxDevice::FreeHostWriteRegion", GetLogger(), Canvas::LogLevel::Debug);

    if (!suballocation.pBuffer)
        return;

    if (suballocation.AllocationKey == 0)
    {
        // Unpooled (oversized): just release
        suballocation = {};
        return;
    }

    // Return to bucket pool
    uint32_t bucketIndex = static_cast<uint32_t>(suballocation.AllocationKey) - 1;
    if (bucketIndex < kNumBuckets)
    {
        auto& bucket = m_HostWriteBuckets[bucketIndex];
        if (bucket.size() < kBucketPoolCap)
        {
            bucket.push_back(std::move(suballocation.pBuffer));
        }
        // else: drop the ref — buffer released when TGemPtr goes out of scope
    }

    suballocation = {};
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateMeshData(
    [[maybe_unused]] uint32_t vertexCount,
    [[maybe_unused]] const Canvas::Math::FloatVector4 *positions,
    [[maybe_unused]] const Canvas::Math::FloatVector4 *normals,
    [[maybe_unused]] Canvas::XGfxRenderQueue *pRenderQueue,
    [[maybe_unused]] Canvas::XGfxMeshData **ppMesh)
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
        // Create device-local (default heap) buffers for positions and normals
        Gem::TGemPtr<Canvas::XGfxBuffer> pPosBuffer;
        Gem::TGemPtr<Canvas::XGfxBuffer> pNormBuffer;

        Gem::ThrowGemError(CreateBuffer(posSize, Canvas::GfxMemoryUsage::DeviceLocal, &pPosBuffer));
        Gem::ThrowGemError(CreateBuffer(normSize, Canvas::GfxMemoryUsage::DeviceLocal, &pNormBuffer));

        // Allocate space in the host-write (upload) buffer pool.
        Canvas::GfxResourceAllocation suballocation;
        Gem::ThrowGemError(AllocateHostWriteRegion(allocationSize, suballocation));

        // Copy data into the upload buffer immediately on CPU timeline
        {
            auto pHostBufImpl = static_cast<CBuffer12*>(suballocation.pBuffer.Get());
            ID3D12Resource* pHostResource = pHostBufImpl ? pHostBufImpl->GetD3DResource() : nullptr;
            if (pHostResource)
            {
                void* pMapped = nullptr;
                HRESULT hr = pHostResource->Map(0, nullptr, &pMapped);
                if (SUCCEEDED(hr) && pMapped)
                {
                    uint64_t posBytes = posSize;
                    uint64_t normBytes = normSize;

                    uint8_t* dst = static_cast<uint8_t*>(pMapped) + suballocation.Offset;
                    if (positions && posBytes > 0)
                        memcpy(dst, positions, static_cast<size_t>(posBytes));
                    if (normals && normBytes > 0)
                        memcpy(dst + posBytes, normals, static_cast<size_t>(normBytes));

                    pHostResource->Unmap(0, nullptr);
                }
            }
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

            // Schedule GPU copy operations from upload buffer to device-local buffers
            pRQ->RecordCommandBlock(
                usages.Build(),
                [pPosBuffer, pNormBuffer, suballocation, posSize, normSize](ID3D12GraphicsCommandList* cmdList)
                {
                    ID3D12Resource* pSrc = static_cast<CBuffer12*>(suballocation.pBuffer.Get())->GetD3DResource();
                    ID3D12Resource* pDstPos = static_cast<CBuffer12*>(pPosBuffer.Get())->GetD3DResource();
                    ID3D12Resource* pDstNorm = static_cast<CBuffer12*>(pNormBuffer.Get())->GetD3DResource();

                    if (pSrc && pDstPos)
                        cmdList->CopyBufferRegion(pDstPos, 0, pSrc, suballocation.Offset, posSize);
                    if (pSrc && pDstNorm)
                        cmdList->CopyBufferRegion(pDstNorm, 0, pSrc, suballocation.Offset + posSize, normSize);
                },
                "Upload Mesh Data");

            // Schedule release of the host-write region after the next submit completes
            pRQ->RetireUploadAllocation(suballocation);
        }

        // Create and register the CMeshData12 object that holds the buffers
        Gem::TGemPtr<CMeshData12> pMeshData;
        Gem::ThrowGemError(TGfxElement<Canvas::XGfxMeshData>::CreateAndRegister(&pMeshData, GetCanvas()));
        
        pMeshData->SetPositionBuffer(pPosBuffer);
        pMeshData->SetNormalBuffer(pNormBuffer);
        
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
    Canvas::XGfxMeshData **ppMesh)
{
    return CreateMeshData(vertexCount, positions, normals, pRenderQueue, ppMesh);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::AllocVertexBuffer(uint32_t vertexCount, uint32_t vertexStride, const void* pVertexData, Canvas::XGfxRenderQueue* pRQ, Canvas::GfxResourceAllocation& out)
{
    if (vertexCount == 0 || vertexStride == 0 || !pVertexData || !pRQ)
        return Gem::Result::InvalidArg;

    try
    {
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
            &pBuffer, GetCanvas(), alloc.pResource, nullptr));

        out.pBuffer       = pBuffer;
        out.Offset        = 0;
        out.Size          = dataSize;
        out.AllocationKey = alloc.AllocationKey;

        // Stage the upload via the render queue
        auto* pRQ12 = static_cast<CRenderQueue12*>(pRQ);
        Gem::ThrowGemError(pRQ12->StageBufferUpload(out, pVertexData, dataSize));

        return Gem::Result::Success;
    }
    catch (Gem::GemError& e) { return e.Result(); }
    catch (_com_error& e) { return ResultFromHRESULT(e.Error()); }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CDevice12::FreeVertexBuffer(const Canvas::GfxResourceAllocation& suballoc)
{
    if (suballoc.AllocationKey == 0)
        return;

    ResourceAllocation alloc;
    alloc.AllocationKey = suballoc.AllocationKey;
    uint32_t blockStart;
    ResourceAllocation::DecodeKey(suballoc.AllocationKey, blockStart, alloc.AllocatorTier, alloc.SizeInUnits);
    m_ResourceAllocator.Free(alloc);
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
