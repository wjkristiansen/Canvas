//================================================================================================
// Device12
//================================================================================================

#include "stdafx.h"

#include "GraphicsContext12.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CDevice12::CDevice12()
{
}

//------------------------------------------------------------------------------------------------
Result CDevice12::Initialize()
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
        CInstance12::GetSingleton()->Logger().LogErrorF("CDevice12::Initialize: HRESULT 0x%08x", e.Error());
        return GemResult(e.Error());
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice12::CreateGraphicsContext(XGfxGraphicsContext **ppContext)
{
    CFunctionSentinel Sentinel(CInstance12::GetSingleton()->Logger(), "XGfxDevice::CreateGraphicsContext");
    try
    {
        Gem::TGemPtr<XGfxGraphicsContext> pContext(new TGeneric<CGraphicsContext12>(this));
        return pContext->QueryInterface(ppContext);
    }
    catch (const Gem::GemError &e)
    {
        Sentinel.ReportError(e.Result());
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

////------------------------------------------------------------------------------------------------
//GEMMETHODIMP CDevice12::CreateMaterial(const ModelData::MATERIAL_DATA *pMaterialData, XMaterial **ppMaterial)
//{
//    return Result::NotImplemented;
//}
