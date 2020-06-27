//================================================================================================
// GraphicsDevice12
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

DXGI_FORMAT CanvasFormatToDXGIFormat(Canvas::GfxFormat Fmt)
{
    switch (Fmt)
    {
        case GfxFormat::Unknown: return DXGI_FORMAT_UNKNOWN;
        case GfxFormat::RGBA32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case GfxFormat::RGBA32_UInt: return DXGI_FORMAT_R32G32B32A32_UINT;
        case GfxFormat::RGBA32_Int: return DXGI_FORMAT_R32G32B32A32_SINT;
        case GfxFormat::RGB32_Float: return DXGI_FORMAT_R32G32B32_FLOAT;
        case GfxFormat::RGB32_UInt: return DXGI_FORMAT_R32G32B32_UINT;
        case GfxFormat::RGB32_Int: return DXGI_FORMAT_R32G32B32_SINT;
        case GfxFormat::RG32_Float: return DXGI_FORMAT_R32G32_FLOAT;
        case GfxFormat::RG32_UInt: return DXGI_FORMAT_R32G32_UINT;
        case GfxFormat::RG32_Int: return DXGI_FORMAT_R32G32_SINT;
        case GfxFormat::D32_Float: return DXGI_FORMAT_D32_FLOAT;
        case GfxFormat::R32_Float: return DXGI_FORMAT_R32_FLOAT;
        case GfxFormat::R32_UInt: return DXGI_FORMAT_R32_UINT;
        case GfxFormat::R32_Int: return DXGI_FORMAT_R32_SINT;
        case GfxFormat::RGBA16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case GfxFormat::RGBA16_UInt: return DXGI_FORMAT_R16G16B16A16_UINT;
        case GfxFormat::RGBA16_Int: return DXGI_FORMAT_R16G16B16A16_SINT;
        case GfxFormat::RGBA16_UNorm: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case GfxFormat::RGBA16_Norm: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case GfxFormat::RG16_Float: return DXGI_FORMAT_R16G16_FLOAT;
        case GfxFormat::RG16_UInt: return DXGI_FORMAT_R16G16_UINT;
        case GfxFormat::RG16_Int: return DXGI_FORMAT_R16G16_SINT;
        case GfxFormat::RG16_UNorm: return DXGI_FORMAT_R16G16_UNORM;
        case GfxFormat::RG16_Norm: return DXGI_FORMAT_R16G16_SNORM;
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
        case GfxFormat::RGB10A2_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case GfxFormat::RGB10A2_UInt: return DXGI_FORMAT_R8G8B8A8_UINT;
        case GfxFormat::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case GfxFormat::RGBA8_UInt: return DXGI_FORMAT_R8G8B8A8_UINT;
        case GfxFormat::RGBA8_Norm: return DXGI_FORMAT_R8G8B8A8_SNORM;
        case GfxFormat::RGBA8_Int: return DXGI_FORMAT_R8G8B8A8_SINT;
        case GfxFormat::RG8_UNorm: return DXGI_FORMAT_R8G8_UNORM;
        case GfxFormat::RG8_UInt: return DXGI_FORMAT_R8G8_UINT;
        case GfxFormat::RG8_Norm: return DXGI_FORMAT_R8G8_SNORM;
        case GfxFormat::RG8_Int: return DXGI_FORMAT_R8G8_SINT;
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
Result CDevice::Initialize(HWND hWnd, bool Windowed)
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

        // Create the DXGI factory
        CComPtr<IDXGIFactory7> pFactory;
        ThrowFailedHResult(CreateDXGIFactory2(0, IID_PPV_ARGS(&pFactory)));

        // Create the swap chain
        CComPtr<IDXGISwapChain1> pSwapChain1;
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = 4;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swapChainDesc.BufferUsage = DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.Scaling = DXGI_SCALING_NONE;
        swapChainDesc.Width = 0;
        swapChainDesc.Height = 0;
        swapChainDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainDescFS = {};
        swapChainDescFS.RefreshRate.Denominator = 1;
        swapChainDescFS.Windowed = Windowed;
        ThrowFailedHResult(pFactory->CreateSwapChainForHwnd(pCommandQueue, hWnd, &swapChainDesc, &swapChainDescFS, nullptr, &pSwapChain1));

        CComPtr<IDXGISwapChain4> pSwapChain4;
        ThrowFailedHResult(pSwapChain1->QueryInterface(&pSwapChain4));

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
        DefaultRootParams[3].InitAsDescriptorTable(1, DefaultDescriptorRanges.data(), D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC DefaultRootSigDesc(4U, DefaultRootParams.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        CComPtr<ID3DBlob> pRSBlob;
        ThrowFailedHResult(D3D12SerializeVersionedRootSignature(&DefaultRootSigDesc, &pRSBlob, nullptr));

        CComPtr<ID3D12RootSignature> pDefaultRootSig;
        pDevice->CreateRootSignature(1, pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(), IID_PPV_ARGS(&pDefaultRootSig));

        m_pD3DDevice = std::move(pDevice);
        m_pDirectCommandQueue = std::move(pCommandQueue);
        m_pDXGIFactory = std::move(pFactory);
        m_pSwapChain = std::move(pSwapChain4);
        m_pDefaultRootSig = std::move(pDefaultRootSig);
    }
    catch (_com_error &e)
    {
        m_Logger.LogErrorF("CDevice::Initialize: HRESULT 0x%08x", e.Error());
        return HResultToResult(e.Error());
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice::Present()
{
    try
    {
        CComPtr<ID3D12Resource> pBackBuffer;
        UINT bbindex = m_pSwapChain->GetCurrentBackBufferIndex();
        ThrowFailedHResult(m_pSwapChain->GetBuffer(bbindex, IID_PPV_ARGS(&pBackBuffer)));
        D3D12_DESCRIPTOR_HEAP_DESC dhDesc = {};
        dhDesc.NumDescriptors = 1;
        dhDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        CComPtr<ID3D12DescriptorHeap> pRTVHeap;
        ThrowFailedHResult(m_pD3DDevice->CreateDescriptorHeap(&dhDesc, IID_PPV_ARGS(&pRTVHeap)));
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        m_pD3DDevice->CreateRenderTargetView(pBackBuffer, &rtvDesc, pRTVHeap->GetCPUDescriptorHandleForHeapStart());

        CComPtr<ID3D12GraphicsCommandList> pCommandList;
        CComPtr<ID3D12CommandAllocator> pCommandAllocator;
        ThrowFailedHResult(m_pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCommandAllocator)));
        ThrowFailedHResult(m_pD3DDevice->CreateCommandList1(1, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&pCommandList)));

        const float ClearColors[2][4] =
        {
            { 1.f, 0.f, 0.f, 0.f },
            { 0.f, 0.f, 1.f, 0.f },
        };

        static UINT clearColorIndex = 0;

        pCommandList->Reset(pCommandAllocator, nullptr);
        D3D12_RESOURCE_BARRIER RTBarrier[1] = {};
        RTBarrier[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        RTBarrier[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        RTBarrier[0].Transition.pResource = pBackBuffer;
        RTBarrier[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        RTBarrier[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        pCommandList->ResourceBarrier(1, RTBarrier);
        pCommandList->ClearRenderTargetView(pRTVHeap->GetCPUDescriptorHandleForHeapStart(), ClearColors[clearColorIndex], 0, nullptr);
        RTBarrier[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        RTBarrier[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        pCommandList->ResourceBarrier(1, RTBarrier);
        clearColorIndex ^= 1;
        ThrowFailedHResult(pCommandList->Close());

        ID3D12CommandList *pExList[] =
        {
            pCommandList
        };

        m_pDirectCommandQueue->ExecuteCommandLists(1, pExList);

        ThrowFailedHResult(m_pSwapChain->Present(0, 0));

        CComPtr<ID3D12Fence> pFence;
        ThrowFailedHResult(m_pD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence)));
        m_pDirectCommandQueue->Signal(pFence, 1);
        HANDLE hEvent = CreateEvent(nullptr, 0, 0, nullptr);
        pFence->SetEventOnCompletion(1, hEvent);
        WaitForSingleObject(hEvent, INFINITE);
        CloseHandle(hEvent);
    }
    catch (_com_error &e)
    {
        m_Logger.LogErrorF("CDevice::Present: HRESULT 0x%08x", e.Error());
        return HResultToResult(e.Error());
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDevice::AllocateGraphicsContext(XCanvasGfxContext **ppContext)
{
    try
    {
        Gem::TGemPtr<XCanvasGfxContext> pContext(new TGeneric<CContext>(this, D3D12_COMMAND_LIST_TYPE_DIRECT));
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
        auto result = pGraphicsDevice->Initialize(hWnd, true);
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

//------------------------------------------------------------------------------------------------
CUploadBuffer::CUploadBuffer(ID3D12Resource *pResource, UINT64 OffsetToStart, UINT64 Size) :
    m_pResource(pResource),
    m_OffsetToStart(OffsetToStart)
{
    // Persistently map the data
    D3D12_RANGE ReadRange;
    ReadRange.Begin = SIZE_T(OffsetToStart);
    ReadRange.End = SIZE_T(OffsetToStart + Size);
    ThrowFailedHResult(pResource->Map(0, &ReadRange, &m_pData));
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void *) CUploadBuffer::Data()
{
    return m_pData;
}
