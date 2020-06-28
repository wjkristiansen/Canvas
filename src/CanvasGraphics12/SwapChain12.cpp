//================================================================================================
// SwapChain12
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

CSwapChain::CSwapChain(HWND hWnd, bool Windowed, ID3D12Device *pDevice, ID3D12CommandQueue *pCommandQueue)
{
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

    CComPtr<ID3D12Resource> pBackBuffer;
    UINT bbindex = pSwapChain4->GetCurrentBackBufferIndex();
    ThrowFailedHResult(pSwapChain4->GetBuffer(bbindex, IID_PPV_ARGS(&pBackBuffer)));
    TGemPtr<CSurface> pSurface = new TGeneric<CSurface>(pBackBuffer);
        
    CComPtr<ID3D12Fence> pFence;
    ThrowFailedHResult(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence)));

    m_pCommandQueue = pCommandQueue;
    m_pFence.Attach(pFence.Detach());
    m_pDXGIFactory.Attach(pFactory.Detach());
    m_pSwapChain.Attach(pSwapChain4.Detach());
    m_pSurface = std::move(pSurface.Detach());
}

GEMMETHODIMP CSwapChain::Present()
{
    try
    {
        //D3D12_DESCRIPTOR_HEAP_DESC dhDesc = {};
        //dhDesc.NumDescriptors = 1;
        //dhDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        //CComPtr<ID3D12DescriptorHeap> pRTVHeap;
        //ThrowFailedHResult(m_pD3DDevice->CreateDescriptorHeap(&dhDesc, IID_PPV_ARGS(&pRTVHeap)));
        //D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        //rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        //rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        //m_pD3DDevice->CreateRenderTargetView(pBackBuffer, &rtvDesc, pRTVHeap->GetCPUDescriptorHandleForHeapStart());

        //CComPtr<ID3D12GraphicsCommandList> pCommandList;
        //CComPtr<ID3D12CommandAllocator> pCommandAllocator;
        //ThrowFailedHResult(m_pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCommandAllocator)));
        //ThrowFailedHResult(m_pD3DDevice->CreateCommandList1(1, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&pCommandList)));

        //const float ClearColors[2][4] =
        //{
        //    { 1.f, 0.f, 0.f, 0.f },
        //    { 0.f, 0.f, 1.f, 0.f },
        //};

        //static UINT clearColorIndex = 0;

        //pCommandList->Reset(pCommandAllocator, nullptr);
        //D3D12_RESOURCE_BARRIER RTBarrier[1] = {};
        //RTBarrier[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        //RTBarrier[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        //RTBarrier[0].Transition.pResource = pBackBuffer;
        //RTBarrier[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        //RTBarrier[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        //pCommandList->ResourceBarrier(1, RTBarrier);
        //pCommandList->ClearRenderTargetView(pRTVHeap->GetCPUDescriptorHandleForHeapStart(), ClearColors[clearColorIndex], 0, nullptr);
        //RTBarrier[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        //RTBarrier[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        //pCommandList->ResourceBarrier(1, RTBarrier);
        //clearColorIndex ^= 1;
        //ThrowFailedHResult(pCommandList->Close());

        //ID3D12CommandList *pExList[] =
        //{
        //    pCommandList
        //};

        //m_pDirectCommandQueue->ExecuteCommandLists(1, pExList);

        std::unique_lock<std::mutex> Lock(m_mutex);

        // Queue the Present
        ThrowFailedHResult(m_pSwapChain->Present(0, 0));

        // Get the new back buffer
        CComPtr<ID3D12Resource> pBackBuffer;
        UINT bbindex = m_pSwapChain->GetCurrentBackBufferIndex();
        ThrowFailedHResult(m_pSwapChain->GetBuffer(bbindex, IID_PPV_ARGS(&pBackBuffer)));
        m_pSurface->Rename(pBackBuffer);

        // Increment the swapchain fence value and queue a signal
        m_FenceValue++;
        m_pCommandQueue->Signal(m_pFence, m_FenceValue);
    }
    catch (_com_error &e)
    {
        //m_Logger.LogErrorF("CDevice::Present: HRESULT 0x%08x", e.Error());
        return HResultToResult(e.Error());
    }

    return Result::Success;
}

GEMMETHODIMP CSwapChain::GetSurface(XCanvasGfxSurface **ppSurface)
{
    return m_pSurface->QueryInterface(ppSurface);
}

GEMMETHODIMP CSwapChain::WaitForLastPresent()
{
    std::unique_lock<std::mutex> Lock(m_mutex);
    HANDLE hEvent = CreateEvent(nullptr, 0, 0, nullptr);
    m_pFence->SetEventOnCompletion(m_FenceValue, hEvent);
    WaitForSingleObject(hEvent, INFINITE);
    CloseHandle(hEvent);
    return Result::Success;
}
