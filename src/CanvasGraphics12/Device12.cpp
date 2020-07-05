//================================================================================================
// Device12
//================================================================================================

#include "stdafx.h"

#include "GraphicsContext12.h"

using namespace Canvas;

DXGI_FORMAT CanvasFormatToDXGIFormat(Canvas::GfxFormat Fmt)
{
    switch (Fmt)
    {
        case GfxFormat::Unknown: return DXGI_FORMAT_UNKNOWN;
        case GfxFormat::R32G32B32A32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case GfxFormat::R32G32B32A32_UInt: return DXGI_FORMAT_R32G32B32A32_UINT;
        case GfxFormat::R32G32B32A32_Int: return DXGI_FORMAT_R32G32B32A32_SINT;
        case GfxFormat::R32G32B32_Float: return DXGI_FORMAT_R32G32B32_FLOAT;
        case GfxFormat::R32G32B32_UInt: return DXGI_FORMAT_R32G32B32_UINT;
        case GfxFormat::R32G32B32_Int: return DXGI_FORMAT_R32G32B32_SINT;
        case GfxFormat::R32G32_Float: return DXGI_FORMAT_R32G32_FLOAT;
        case GfxFormat::R32G32_UInt: return DXGI_FORMAT_R32G32_UINT;
        case GfxFormat::R32G32_Int: return DXGI_FORMAT_R32G32_SINT;
        case GfxFormat::D32_Float: return DXGI_FORMAT_D32_FLOAT;
        case GfxFormat::R32_Float: return DXGI_FORMAT_R32_FLOAT;
        case GfxFormat::R32_UInt: return DXGI_FORMAT_R32_UINT;
        case GfxFormat::R32_Int: return DXGI_FORMAT_R32_SINT;
        case GfxFormat::R16G16B16A16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case GfxFormat::R16G16B16A16_UInt: return DXGI_FORMAT_R16G16B16A16_UINT;
        case GfxFormat::R16G16B16A16_Int: return DXGI_FORMAT_R16G16B16A16_SINT;
        case GfxFormat::R16G16B16A16_UNorm: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case GfxFormat::R16G16B16A16_Norm: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case GfxFormat::R16G16_Float: return DXGI_FORMAT_R16G16_FLOAT;
        case GfxFormat::R16G16_UInt: return DXGI_FORMAT_R16G16_UINT;
        case GfxFormat::R16G16_Int: return DXGI_FORMAT_R16G16_SINT;
        case GfxFormat::R16G16_UNorm: return DXGI_FORMAT_R16G16_UNORM;
        case GfxFormat::R16G16_Norm: return DXGI_FORMAT_R16G16_SNORM;
        case GfxFormat::R16_Float: return DXGI_FORMAT_R16_FLOAT;
        case GfxFormat::R16_UInt: return DXGI_FORMAT_R16_UINT;
        case GfxFormat::R16_Int: return DXGI_FORMAT_R16_SINT;
        case GfxFormat::D16_UNorm: return DXGI_FORMAT_D16_UNORM;
        case GfxFormat::R16_UNorm: return DXGI_FORMAT_R16_UNORM;
        case GfxFormat::R16_Norm: return DXGI_FORMAT_R16_SNORM;
        case GfxFormat::D32_Float_S8_UInt_X24: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case GfxFormat::R32_Float_X32: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case GfxFormat::D24_Unorm_S8_Uint: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case GfxFormat::R24_Unorm_X8: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case GfxFormat::X24_S8_UInt: return DXGI_FORMAT_X24_TYPELESS_G8_UINT;
        case GfxFormat::R10G10B10A2_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case GfxFormat::R10G10B10A2_UInt: return DXGI_FORMAT_R8G8B8A8_UINT;
        case GfxFormat::R8G8B8A8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case GfxFormat::R8G8B8A8_UInt: return DXGI_FORMAT_R8G8B8A8_UINT;
        case GfxFormat::R8G8B8A8_Norm: return DXGI_FORMAT_R8G8B8A8_SNORM;
        case GfxFormat::R8G8B8A8_Int: return DXGI_FORMAT_R8G8B8A8_SINT;
        case GfxFormat::R8G8B8_UNorm: return DXGI_FORMAT_R8G8_UNORM;
        case GfxFormat::R8G8B8_UInt: return DXGI_FORMAT_R8G8_UINT;
        case GfxFormat::R8G8B8_Norm: return DXGI_FORMAT_R8G8_SNORM;
        case GfxFormat::R8G8B8_Int: return DXGI_FORMAT_R8G8_SINT;
        case GfxFormat::BC1_UNorm: return DXGI_FORMAT_BC1_UNORM;
        case GfxFormat::BC2_UNorm: return DXGI_FORMAT_BC2_UNORM;
        case GfxFormat::BC3_UNorm: return DXGI_FORMAT_BC3_UNORM;
        case GfxFormat::BC4_UNorm: return DXGI_FORMAT_BC4_UNORM;
        case GfxFormat::BC4_Norm: return DXGI_FORMAT_BC4_SNORM;
        case GfxFormat::BC5_UNorm: return DXGI_FORMAT_BC5_UNORM;
        case GfxFormat::BC5_Norm: return DXGI_FORMAT_BC5_SNORM;
        case GfxFormat::BC7_UNorm: return DXGI_FORMAT_BC7_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}


//------------------------------------------------------------------------------------------------
CDevice::CDevice(QLog::CLogClient *pLogClient) :
    m_Logger(pLogClient, "CANVAS GRAPHICS")
{
}

//------------------------------------------------------------------------------------------------
Result CDevice::Initialize()
{
    try
    {
#if defined(_DEBUG)
        CComPtr<ID3D12Debug3> pDebug;
        ThrowFailedHResult(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug)));
        pDebug->EnableDebugLayer();
#endif
        // Create the device
        CComPtr<ID3D12Device5> pDevice;
        ThrowFailedHResult(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice)));

        // Create the direct command queue
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.NodeMask = 0x1;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        CComPtr<ID3D12CommandQueue> pCommandQueue;
        ThrowFailedHResult(pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pCommandQueue)));

        m_pD3DDevice = std::move(pDevice);
    }
    catch (_com_error &e)
    {
        m_Logger.LogErrorF("CDevice::Initialize: HRESULT 0x%08x", e.Error());
        return HResultToResult(e.Error());
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice::CreateGfxContext(HWND hWnd, bool Windowed, XCanvasGfxContext **ppContext)
{
    try
    {
        Gem::TGemPtr<XCanvasGfxContext> pContext(new TGeneric<CGraphicsContext>(hWnd, Windowed, m_pD3DDevice));
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
//GEMMETHODIMP CDevice::AllocateUploadBuffer(UINT64 SizeInBytes, XCanvasGfxUploadBuffer **ppUploadBuffer)
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
//        m_Logger.LogError("CDevice::AllocateUploadBuffer: Out of memory");
//        return Result::OutOfMemory;
//    }
//    catch (_com_error &e)
//    {
//        m_Logger.LogErrorF("CDevice::AllocateUploadBuffer: HRESULT 0x%08x", e.Error());
//        return HResultToResult(e.Error());
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

//GEMMETHODIMP CDevice::CreateStaticMesh(const ModelData::STATIC_MESH_DATA *pMeshData, XMesh **ppMesh)
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
//        m_Logger.LogErrorF("CDevice::CreateStaticMesh: HRESULT 0x%08x", e.Error());
//        return HResultToResult(e.Error());
//    }
//    return Result::Success;
//}

////------------------------------------------------------------------------------------------------
//GEMMETHODIMP CDevice::CreateMaterial(const ModelData::MATERIAL_DATA *pMaterialData, XMaterial **ppMaterial)
//{
//    return Result::NotImplemented;
//}

//------------------------------------------------------------------------------------------------
Result GEMAPI CreateCanvasGraphicsDevice(_Outptr_result_nullonfailure_ XCanvasGfxDevice **ppGraphicsDevice, HWND hWnd, QLog::CLogClient *pLogClient)
{
    *ppGraphicsDevice = nullptr;

    try
    {
        TGemPtr<CDevice> pGraphicsDevice = new TGeneric<CDevice>(pLogClient); // throw(bad_alloc)
        auto result = pGraphicsDevice->Initialize();
        if (result == Result::Success)
        {
            *ppGraphicsDevice = pGraphicsDevice;
            pGraphicsDevice.Detach();
        }
        return result;
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
}
