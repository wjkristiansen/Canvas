//================================================================================================
// Device12
//================================================================================================

#include "pch.h"

#include "RenderQueue12.h"
#include "Surface12.h"
#include "Buffer12.h"

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
        CComPtr<ID3D12Device5> pDevice;
        ThrowFailedHResult(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice)));
        m_pD3DDevice.Attach(pDevice.Detach());

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
GEMMETHODIMP CDevice12::CreateSurface(const Canvas::GfxSurfaceDesc &desc, Canvas::XGfxSurface **ppSurface)
{
    Canvas::CFunctionSentinel sentinel("XGfxDevice::CreateSurface", GetLogger());
    
    if (!ppSurface)
        return Gem::Result::BadPointer;
    
    *ppSurface = nullptr;
    
    try
    {
        // Convert GfxSurfaceDesc to D3D12_RESOURCE_DESC
        D3D12_RESOURCE_DESC resourceDesc = {};
        
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
        D3D12_RESOURCE_STATES initialState;
        
        if (desc.Flags & Canvas::SurfaceFlag_CpuUpload)
        {
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        }
        else if (desc.Flags & Canvas::SurfaceFlag_CpuReadback)
        {
            heapProps.Type = D3D12_HEAP_TYPE_READBACK;
            initialState = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        else
        {
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
            initialState = D3D12_RESOURCE_STATE_COMMON;
        }
        
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;
        
        // Create the resource
        CComPtr<ID3D12Resource> pResource;
        ThrowFailedHResult(m_pD3DDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(&pResource)));
        
        // Create and register the CSurface12 wrapper
        Gem::TGemPtr<CSurface12> pSurface;
        Gem::ThrowGemError(TGfxElement<CSurface12>::CreateAndRegister<CSurface12>(&pSurface, GetCanvas(), pResource, initialState, nullptr));
        
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
        
        // Create the resource
        CComPtr<ID3D12Resource> pResource;
        ThrowFailedHResult(m_pD3DDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
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
GEMMETHODIMP CDevice12::CreateDebugMesh(
    [[maybe_unused]] uint32_t vertexCount,
    [[maybe_unused]] const Canvas::Math::FloatVector4 *positions,
    [[maybe_unused]] const Canvas::Math::FloatVector4 *normals,
    [[maybe_unused]] Canvas::XGfxMesh **ppMesh)
{
    uint64_t allocationSize = vertexCount * 4 * sizeof(float) * 2;

    // Allocate space in the host write memory
    Canvas::GfxSuballocation suballocation;
    Gem::ThrowGemError(AllocateHostWriteRegion(allocationSize, suballocation));

    return Gem::Result::NotImplemented;
}
