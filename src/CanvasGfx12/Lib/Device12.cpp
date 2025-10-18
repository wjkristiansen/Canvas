//================================================================================================
// Device12
//================================================================================================

#include "pch.h"

#include "RenderQueue12.h"
#include "Surface12.h"
#include "Buffer12.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CDevice12::CDevice12()
{
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
    }
    catch (_com_error &e)
    {
        auto pLogger = GetCanvasLogger();

        if (pLogger)
        {
            pLogger->Error("CDevice12::Initialize: HRESULT 0x%08x", e.Error());
        }
        return Gem::GemResult(e.Error());
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateRenderQueue(XGfxRenderQueue **ppRenderQueue)
{
    CFunctionSentinel Sentinel("XGfxDevice::CreateRenderQueue");
    try
    {
        Gem::TGemPtr<CRenderQueue12> pRenderQueue;
        Gem::ThrowGemError(Gem::TGenericImpl<CRenderQueue12>::Create(&pRenderQueue, this)); // throw(Gem::GemError)
        return pRenderQueue->QueryInterface(ppRenderQueue);
    }
    catch (const Gem::GemError &e)
    {
        Sentinel.SetResultCode(e.Result());
        return e.Result();
    }
}

//------------------------------------------------------------------------------------------------
//GEMMETHODIMP CDevice12::AllocateUploadBuffer(UINT64 SizeInBytes, XGfxUploadBuffer **ppUploadBuffer)
//{
//    try
//    {
//        // Allocate a buffer to contain the vertex data
//        CD3DX12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(SizeInBytes);
//        CComPtr<ID3D12Resource> pD3DBuffer;
//        CD3DX12_HEAP_PROPERTIES HeapProp(D3D12_HEAP_TYPE_UPLOAD);
//        ThrowFailedHResult(m_pD3DDevice->CreateCommittedResource1(&HeapProp, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &BufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, nullptr, IID_PPV_ARGS(&pD3DBuffer)));
//        TGemPtr<CUploadBuffer> pUploadBuffer = new CUploadBuffer(pD3DBuffer, 0, SizeInBytes); // throw(std::bad_alloc), throw(_com_error)
//        *ppUploadBuffer = pUploadBuffer;
//        pUploadBuffer.Detach();
//    }
//    catch (std::bad_alloc)
//    {
//        m_Logger.LogError("CDevice12::AllocateUploadBuffer: Out of memory");
//        return Result::OutOfMemory;
//    }
//    catch (_com_error &e)
//    {
//        m_Logger.LogErrorF("CDevice12::AllocateUploadBuffer: HRESULT 0x%08x", e.Error());
//        return GemResult(e.Error());
//    }
//    return Result::Success;
//}


//------------------------------------------------------------------------------------------------
// |     PosX      |     PosY      |     PosZ      |
// -------------------------------------------------
// |    NormX      |    NormY      |    NormZ      |
// -------------------------------------------------
// |      U0       |      V0       |
// ---------------------------------
// |      U1       |      V1       |
// ---------------------------------
// |      U2       |      V2       |
// ---------------------------------
//
// Vertex data does not use per-vertex colors.  All color data
// comes from the material.
//
// Bone indices from the source mesh are ignored.  Use
// CreateSkinMesh for meshes with bone weights.
struct Vertex
{
    float Position[3];
    float Normal[3];
    float TexCoords[3][2];
};

//GEMMETHODIMP CDevice12::CreateStaticMesh(const ModelData::STATIC_MESH_DATA *pMeshData, XMesh **ppMesh)
//{
//    try
//    {
//        if (pMeshData->pVertices)
//        {
//            UINT64 BufferSize = sizeof(Vertex) * pMeshData->NumVertices;
//            TGemPtr<CUploadBuffer> pVertexBuffer;
//            AllocateUploadBuffer(BufferSize, &pVertexBuffer);
//            Vertex *pVertices = reinterpret_cast<Vertex *>(pVertexBuffer->Data());

//            // Copy the vertex data into the buffer, converting from float3 to float4 format
//            for (UINT i = 0; i < pMeshData->NumVertices; ++i)
//            {
//                pVertices[i].Position[0] = pMeshData->pVertices[i][0];
//                pVertices[i].Position[1] = pMeshData->pVertices[i][1];
//                pVertices[i].Position[2] = pMeshData->pVertices[i][2];

//                if (pMeshData->pNormals)
//                {
//                    pVertices[i].Normal[0] = pMeshData->pNormals[i][0];
//                    pVertices[i].Normal[1] = pMeshData->pNormals[i][1];
//                    pVertices[i].Normal[2] = pMeshData->pNormals[i][2];
//                }
//                else
//                {
//                    // Material needs to handle zero-length normal
//                    pVertices[i].Normal[0] = 0;
//                    pVertices[i].Normal[1] = 0;
//                    pVertices[i].Normal[2] = 0;
//                }

//                for (UINT j = 0; j < 4; ++j)
//                {
//                    if (pMeshData->pTextureUVs[j])
//                    {
//                        pVertices[i].TexCoords[j][0] = pMeshData->pTextureUVs[j][i][0];
//                        pVertices[i].TexCoords[j][1] = pMeshData->pTextureUVs[j][i][1];
//                    }
//                    else
//                    {
//                        // Set all texture coordinates to zero
//                        ZeroMemory(pVertices[i].TexCoords, sizeof(pVertices[i].TexCoords));
//                    }
//                }
//            }
//        }
//    }
//    catch (_com_error &e)
//    {
//        m_Logger.LogErrorF("CDevice12::CreateStaticMesh: HRESULT 0x%08x", e.Error());
//        return GemResult(e.Error());
//    }
//    return Result::Success;
//}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateMaterial()
{
    CFunctionSentinel Sentinel("XGfxDevice::CreateMaterial");
    
   return Gem::Result::NotImplemented;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateSurface(const SurfaceDesc &desc, Canvas::XGfxSurface **ppSurface)
{
    CFunctionSentinel Sentinel("XGfxDevice::CreateSurface");
    
    if (!ppSurface)
        return Gem::Result::BadPointer;
    
    *ppSurface = nullptr;
    
    try
    {
        // Convert SurfaceDesc to D3D12_RESOURCE_DESC
        D3D12_RESOURCE_DESC resourceDesc = {};
        
        switch (desc.Dimension)
        {
        case SurfaceDimension::Dimension1D:
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
            resourceDesc.Width = desc.Width;
            resourceDesc.Height = 1;
            resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.ArraySize);
            break;
        case SurfaceDimension::Dimension2D:
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            resourceDesc.Width = desc.Width;
            resourceDesc.Height = desc.Height;
            resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.ArraySize);
            break;
        case SurfaceDimension::Dimension3D:
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            resourceDesc.Width = desc.Width;
            resourceDesc.Height = desc.Height;
            resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.Depth);
            break;
        case SurfaceDimension::DimensionCube:
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
        
        // Set resource flags based on SurfaceFlags
        if (desc.Flags & SurfaceFlag_RenderTarget)
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (desc.Flags & SurfaceFlag_DepthStencil)
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (desc.Flags & SurfaceFlag_UnorderedAccess)
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        
        // Determine heap properties and initial state
        D3D12_HEAP_PROPERTIES heapProps = {};
        D3D12_RESOURCE_STATES initialState;
        
        if (desc.Flags & SurfaceFlag_CpuUpload)
        {
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        }
        else if (desc.Flags & SurfaceFlag_CpuReadback)
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
        
        // Create the CSurface12 wrapper
        Gem::TGemPtr<CSurface12> pSurface;
        Gem::ThrowGemError(Gem::TGenericImpl<CSurface12>::Create(&pSurface, pResource, initialState));
        
        return pSurface->QueryInterface(ppSurface);
    }
    catch (const _com_error &e)
    {
        auto pLogger = GetCanvasLogger();
        if (pLogger)
            pLogger->Error("CDevice12::CreateSurface: HRESULT 0x%08x", e.Error());
        return Gem::GemResult(e.Error());
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateBuffer(UINT sizeInBytes, Canvas::XGfxBuffer **ppBuffer)
{
    CFunctionSentinel Sentinel("XGfxDevice::CreateBuffer");
    
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
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
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
        
        // Create the CBuffer12 wrapper
        Gem::TGemPtr<CBuffer12> pBuffer;
        Gem::ThrowGemError(Gem::TGenericImpl<CBuffer12>::Create(&pBuffer, pResource, D3D12_RESOURCE_STATE_COMMON));
        
        return pBuffer->QueryInterface(ppBuffer);
    }
    catch (const _com_error &e)
    {
        auto pLogger = GetCanvasLogger();
        if (pLogger)
            pLogger->Error("CDevice12::CreateBuffer: HRESULT 0x%08x", e.Error());
        return Gem::GemResult(e.Error());
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }
}

}