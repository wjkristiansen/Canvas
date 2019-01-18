//================================================================================================
// GraphicsDevice12
//================================================================================================

#include "stdafx.h"


//------------------------------------------------------------------------------------------------
Result CGraphicsDevice12::Initialize(HWND hWnd, bool Windowed)
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
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING|DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swapChainDesc.BufferUsage = DXGI_USAGE_SHADER_INPUT|DXGI_USAGE_RENDER_TARGET_OUTPUT;
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

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC DefaultRootSigDesc (4U, DefaultRootParams.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        CComPtr<ID3DBlob> pRSBlob;
        ThrowFailedHResult(D3D12SerializeVersionedRootSignature(&DefaultRootSigDesc, &pRSBlob, nullptr));

        CComPtr<ID3D12RootSignature> pDefaultRootSig;
        pDevice->CreateRootSignature(1, pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(), IID_PPV_ARGS(&pDefaultRootSig));

        // BUGBUG - test code...
        CComPtr<ID3D12Resource> pBuffer;
        CD3DX12_RESOURCE_DESC BufDesc = CD3DX12_RESOURCE_DESC::Buffer(65536);
        CD3DX12_HEAP_PROPERTIES HeapProp(D3D12_HEAP_TYPE_UPLOAD);
        ThrowFailedHResult(pDevice->CreateCommittedResource(
            &HeapProp,
            D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
            &BufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&pBuffer)));
        D3D12_GPU_VIRTUAL_ADDRESS BaseVA = pBuffer->GetGPUVirtualAddress();
        CComPtr<ID3D12GraphicsCommandList4> pCL;
        CComPtr<ID3D12CommandAllocator> pCA;
        ThrowFailedHResult(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCA)));
        ThrowFailedHResult(pDevice->CreateCommandList1(1, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&pCL)));
        pCL->Reset(pCA, nullptr);
        pCL->SetGraphicsRootSignature(pDefaultRootSig);
        pCL->SetGraphicsRootConstantBufferView(0, BaseVA + 514);

        // BUGBUG - end test code

        m_pD3DDevice = std::move(pDevice);
        m_pDirectCommandQueue = std::move(pCommandQueue);
        m_pDXGIFactory = std::move(pFactory);
        m_pSwapChain = std::move(pSwapChain4);
        m_pDefaultRootSig = std::move(pDefaultRootSig);
    }
    catch (_com_error &e)
    {
        return HResultToResult(e.Error());
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
Result CGraphicsDevice12::RenderFrame()
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
        return HResultToResult(e.Error());
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
Result CGraphicsDevice12::AllocateUploadBuffer(UINT64 SizeInBytes, CUploadBuffer **ppUploadBuffer)
{
    try
    {
        // Allocate a buffer to contain the vertex data
        CD3DX12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(SizeInBytes);
        CComPtr<ID3D12Resource> pD3DBuffer;
        CD3DX12_HEAP_PROPERTIES HeapProp(D3D12_HEAP_TYPE_UPLOAD);
        ThrowFailedHResult(m_pD3DDevice->CreateCommittedResource1(&HeapProp, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, &BufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, nullptr, IID_PPV_ARGS(&pD3DBuffer)));
        TGemPtr<CUploadBuffer> pUploadBuffer = new CUploadBuffer12(pD3DBuffer, 0, SizeInBytes); // throw(std::bad_alloc), throw(_com_error)
        *ppUploadBuffer = pUploadBuffer;
        pUploadBuffer.Detach();
    }
    catch (std::bad_alloc)
    {
        return Result::OutOfMemory;
    }
    catch (_com_error &e)
    {
        return HResultToResult(e.Error());
    }
    return Result::Success;
}


//------------------------------------------------------------------------------------------------
Result CGraphicsDevice12::CreateMesh(const MESH_DATA *pMeshData, XMesh **ppMesh)
{
    try
    {
        if (pMeshData->pVertices)
        {
            TGemPtr<CUploadBuffer> pVertexBuffer;
            AllocateUploadBuffer(pMeshData->NumVertices * sizeof(FloatVector4), &pVertexBuffer);
            FloatVector4 *pVertexPos = reinterpret_cast<FloatVector4 *>(pVertexBuffer->Data());

            // Copy the vertex data into the buffer, converting from float3 to float4 format
            for (UINT i = 0; i < pMeshData->NumVertices; ++i)
            {
                const FloatVector3 &SrcPos = pMeshData->pVertices[i];
                pVertexPos[i] = FloatVector4(SrcPos.X(), SrcPos.Y(), SrcPos.Z(), 0.f);
            }
        }
    }
    catch (_com_error &e)
    {
        return HResultToResult(e.Error());
    }
    return Result::Success;
}

//------------------------------------------------------------------------------------------------
Result CGraphicsDevice12::CreateMaterial(const MATERIAL_DATA *pMaterialData)
{
    return Result::NotImplemented;
}

//------------------------------------------------------------------------------------------------
Result GEMAPI CreateGraphicsDevice12(CGraphicsDevice **ppGraphicsDevice, HWND hWnd)
{
    *ppGraphicsDevice = nullptr;

    try
    {
        TGemPtr<CGraphicsDevice12> pGraphicsDevice = new TGeneric<CGraphicsDevice12>(); // throw(bad_alloc)
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
CUploadBuffer12::CUploadBuffer12(ID3D12Resource *pResource, UINT64 OffsetToStart, UINT64 Size) :
    m_pResource(pResource),
    m_OffsetToStart(OffsetToStart)
{
    // Persistently map the data
    D3D12_RANGE ReadRange;
    ReadRange.Begin = OffsetToStart;
    ReadRange.End = OffsetToStart + Size;
    ThrowFailedHResult(pResource->Map(0, &ReadRange, &m_pData));
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void *) CUploadBuffer12::Data()
{
    return m_pData;
}