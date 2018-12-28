//================================================================================================
// GraphicsDevice12
//================================================================================================

#include "stdafx.h"


//------------------------------------------------------------------------------------------------
Result CGraphicsDevice12::Initialize(HWND hWnd, bool Windowed)
{
    try
    {
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

        m_pD3DDevice = pDevice;
        m_pDirectCommandQueue = pCommandQueue;
        m_pDXGIFactory = pFactory;
        m_pSwapChain = pSwapChain4;
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
        pCommandList->ClearRenderTargetView(pRTVHeap->GetCPUDescriptorHandleForHeapStart(), ClearColors[clearColorIndex], 0, nullptr);
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
Result GEMAPI CreateGraphicsDevice12(CGraphicsDevice **ppGraphicsDevice, HWND hWnd)
{
    *ppGraphicsDevice = nullptr;

    try
    {
        CGraphicsDevice12 *pGraphicsDevice = new CGraphicsDevice12(); // throw(bad_alloc)
        auto result = pGraphicsDevice->Initialize(hWnd, true);
        if (result == Result::Success)
        {
            *ppGraphicsDevice = pGraphicsDevice;
        }
        return result;
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
}
