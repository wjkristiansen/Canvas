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
    TGfxElement(pCanvas),
    m_HostWriteSuballocator(m_HostWriteSize)
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

        // Create an upload heap scratch buffer for CPU write operations
        Gem::TGemPtr<Canvas::XGfxBuffer> pScratchBuffer;
        Gem::ThrowGemError(CreateBuffer(m_HostWriteSize, Canvas::GfxMemoryUsage::HostWrite, &pScratchBuffer));
        m_pHostWriteBuffer = pScratchBuffer;
    }
    catch (_com_error &e)
    {
        Canvas::LogError(GetLogger(), "CDevice12::Initialize: HRESULT 0x%08x", e.Error());
        return ResultFromHRESULT(e.Error());
    }

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
void CDevice12::InitializeTextureLayout(ID3D12Resource* pResource, D3D12_BARRIER_LAYOUT initialLayout)
{
    if (!pResource)
    {
        Canvas::LogError(GetLogger(), "InitializeTextureLayout: null resource");
        return;
    }
    
    // Set the committed layout state for a newly created or acquired resource
    // This is called when textures are created or when swap chain buffers are rotated
    m_TextureCurrentLayouts[pResource].SetLayout(0xFFFFFFFF, initialLayout);
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
            // Upload heaps don't have layout - they're always in a CPU-accessible state
            initialLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;
        }
        else if (desc.Flags & Canvas::SurfaceFlag_CpuReadback)
        {
            heapProps.Type = D3D12_HEAP_TYPE_READBACK;
            // Readback heaps don't have layout - they're always in a CPU-accessible state
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
        
        // Create the resource
        CComPtr<ID3D12Resource> pResource;
        ThrowFailedHResult(m_pD3DDevice->CreateCommittedResource3(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            initialLayout,
            nullptr,
            nullptr,
            0,
            nullptr,
            IID_PPV_ARGS(&pResource)));
        
        // Create and register the CSurface12 wrapper (pass COMMON for legacy state tracking)
        Gem::TGemPtr<CSurface12> pSurface;
        Gem::ThrowGemError(TGfxElement<CSurface12>::CreateAndRegister<CSurface12>(&pSurface, GetCanvas(), pResource, D3D12_RESOURCE_STATE_COMMON, nullptr));
        
        // Initialize committed layout tracking
        if (initialLayout != D3D12_BARRIER_LAYOUT_UNDEFINED)
        {
            InitializeTextureLayout(pResource, initialLayout);
        }
        
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
        // Create buffer resource descriptor
        D3D12_RESOURCE_DESC bufferDesc = {};
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
        
        // Create as default heap (GPU-only memory)
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
        Gem::TGemPtr<CBuffer12> pBuffer;
        Gem::ThrowGemError(TGfxElement<CBuffer12>::CreateAndRegister<CBuffer12>(&pBuffer, GetCanvas(), pResource, D3D12_RESOURCE_STATE_COMMON, nullptr));
        
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
GEMMETHODIMP CDevice12::AllocateHostWriteRegion(uint64_t sizeInBytes, Canvas::GfxSuballocation &suballocation)
{
    Canvas::CFunctionSentinel sentinel("XGfxDevice::AllocateHostWriteRegion", GetLogger(), Canvas::LogLevel::Debug);
    
    try
    {
        // Allocate a region from the scratch suballocator
        auto block = m_HostWriteSuballocator.Allocate(sizeInBytes);
        suballocation.Offset = block.Start();
        suballocation.Size = sizeInBytes;
        suballocation.pBuffer = m_pHostWriteBuffer;

        return Gem::Result::Success;
    }
    catch (const BuddySuballocatorException &)
    {
        Canvas::LogError(GetLogger(), "CDevice12::AllocateHostWriteRegion: BuddySuballocatorException");
        sentinel.SetResultCode(Gem::Result::OutOfMemory);
        return Gem::Result::OutOfMemory;
    }
    catch (const Gem::GemError &e)
    {
        sentinel.SetResultCode(e.Result());
        return e.Result();
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CDevice12::FreeHostWriteRegion(Canvas::GfxSuballocation &suballocation)
{
    Canvas::CFunctionSentinel sentinel("XGfxDevice::FreeHostWriteRegion", GetLogger(), Canvas::LogLevel::Debug);
    
    // Free the region back to the scratch suballocator
    auto block = TBuddySuballocator<uint64_t>::ReconstructBlock(suballocation.Offset, suballocation.Size);
    m_HostWriteSuballocator.Free(block);
    suballocation.pBuffer = nullptr;
    suballocation.Offset = 0;
    suballocation.Size = 0;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateDebugMeshData(
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

        // Allocate space in the host-write (upload) scratch buffer
        Canvas::GfxSuballocation suballocation;
        Gem::ThrowGemError(AllocateHostWriteRegion(allocationSize, suballocation));

        // Copy data into the upload buffer immediately on CPU timeline
        {
            auto pHostBufImpl = reinterpret_cast<CBuffer12*>(suballocation.pBuffer.Get());
            ID3D12Resource* pHostResource = pHostBufImpl ? pHostBufImpl->GetD3DResource() : nullptr;
            if (pHostResource)
            {
                void* pMapped = nullptr;
                HRESULT hr = pHostResource->Map(0, nullptr, &pMapped);
                if (SUCCEEDED(hr) && pMapped)
                {
                    uint64_t posBytes = posSize;
                    uint64_t normBytes = normSize;

                    uint8_t* dst = reinterpret_cast<uint8_t*>(pMapped) + suballocation.Offset;
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
            CRenderQueue12* pRQ = reinterpret_cast<CRenderQueue12*>(pRenderQueue);

            // Declare resource usage: destination buffers need barriers for copy
            TaskResourceUsageBuilder usages;
            usages.SetBufferUsage(
                reinterpret_cast<CBuffer12*>(pPosBuffer.Get())->GetD3DResource(),
                D3D12_BARRIER_SYNC_COPY,
                D3D12_BARRIER_ACCESS_COPY_DEST);

            usages.SetBufferUsage(
                reinterpret_cast<CBuffer12*>(pNormBuffer.Get())->GetD3DResource(),
                D3D12_BARRIER_SYNC_COPY,
                D3D12_BARRIER_ACCESS_COPY_DEST);

            // Schedule GPU copy operations from upload buffer to device-local buffers
            Canvas::TaskID copyTask = pRQ->RecordCommands(
                usages.Build(),
                [pPosBuffer, pNormBuffer, suballocation, posSize, normSize](ID3D12GraphicsCommandList* cmdList)
                {
                    ID3D12Resource* pSrc = reinterpret_cast<CBuffer12*>(suballocation.pBuffer.Get())->GetD3DResource();
                    ID3D12Resource* pDstPos = reinterpret_cast<CBuffer12*>(pPosBuffer.Get())->GetD3DResource();
                    ID3D12Resource* pDstNorm = reinterpret_cast<CBuffer12*>(pNormBuffer.Get())->GetD3DResource();

                    if (pSrc && pDstPos)
                        cmdList->CopyBufferRegion(pDstPos, 0, pSrc, suballocation.Offset, posSize);
                    if (pSrc && pDstNorm)
                        cmdList->CopyBufferRegion(pDstNorm, 0, pSrc, suballocation.Offset + posSize, normSize);
                });

            // Schedule release of the host-write region after the copy task completes
            pRQ->ScheduleHostWriteRelease(suballocation, copyTask);
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
