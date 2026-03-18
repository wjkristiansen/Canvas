//================================================================================================
// SwapChain12
//================================================================================================

#include "pch.h"

CSwapChain12::CSwapChain12(Canvas::XCanvas* pCanvas, HWND hWnd, bool Windowed, class CRenderQueue12 *pRenderQueue, DXGI_FORMAT Format, UINT NumBuffers, PCSTR name) :
    TGfxElement(pCanvas),
    m_pRenderQueue(pRenderQueue)
{
    if (name != nullptr)
        SetName(name);
        
    // Create the DXGI factory
    CComPtr<IDXGIFactory7> pFactory;
    ThrowFailedHResult(CreateDXGIFactory2(0, IID_PPV_ARGS(&pFactory)));

    // Detect tearing support
    BOOL tearingSupported = FALSE;
    if (CComPtr<IDXGIFactory5> pFactory5; SUCCEEDED(pFactory->QueryInterface(&pFactory5)))
    {
        (void)pFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported, sizeof(tearingSupported));
    }

    CDevice12 *pDevice = pRenderQueue->GetDevice();
    ID3D12Device *pD3DDevice = pDevice->GetD3DDevice();

    // Create the swap chain
    CComPtr<IDXGISwapChain1> pSwapChain1;
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = NumBuffers;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (tearingSupported)
        swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.BufferUsage = DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Scaling = DXGI_SCALING_NONE;
    swapChainDesc.Width = 0;
    swapChainDesc.Height = 0;
    swapChainDesc.Format = Format; // BUGBUG: Need to use GfxFormat
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainDescFS = {};
    swapChainDescFS.RefreshRate.Denominator = 1;
    swapChainDescFS.Windowed = Windowed;
    ThrowFailedHResult(pFactory->CreateSwapChainForHwnd(pRenderQueue->GetD3DCommandQueue(), hWnd, &swapChainDesc, &swapChainDescFS, nullptr, &pSwapChain1));

    CComPtr<IDXGISwapChain4> pSwapChain4;
    ThrowFailedHResult(pSwapChain1->QueryInterface(&pSwapChain4));

    // Initialize committed layout state for ALL back buffers (not just current one)
    // Swap chain buffers start in COMMON layout by spec
    for (UINT i = 0; i < NumBuffers; i++)
    {
        CComPtr<ID3D12Resource> pBuffer;
        ThrowFailedHResult(pSwapChain4->GetBuffer(i, IID_PPV_ARGS(&pBuffer)));
        pDevice->InitializeTextureLayout(pBuffer, D3D12_BARRIER_LAYOUT_COMMON);
    }

    // Get current back buffer and create surface for it
    CComPtr<ID3D12Resource> pBackBuffer;
    UINT bbindex = pSwapChain4->GetCurrentBackBufferIndex();
    ThrowFailedHResult(pSwapChain4->GetBuffer(bbindex, IID_PPV_ARGS(&pBackBuffer)));

    // Create and register the surface with the canvas
    Gem::TGemPtr<CSurface12> pSurface;
    Gem::ThrowGemError(TGfxElement<CSurface12>::CreateAndRegister(&pSurface, m_pCanvas, pBackBuffer, D3D12_RESOURCE_STATE_COMMON));
    
    // Set the back pointer so the surface knows it belongs to this swap chain
    pSurface->m_pOwnerSwapChain = this;

    CComPtr<ID3D12Fence> pFence;
    ThrowFailedHResult(pD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence)));

    // Configure frame latency waitable object for pacing
    pSwapChain4->SetMaximumFrameLatency(3);
    m_hFrameLatencyWaitableObject = pSwapChain4->GetFrameLatencyWaitableObject();

    m_pFence.Attach(pFence.Detach());
    m_pDXGIFactory.Attach(pFactory.Detach());
    m_pSwapChain.Attach(pSwapChain4.Detach());
    m_pSurface.Attach(pSurface.Detach());
    m_TearingSupported = !!tearingSupported;
}

Gem::Result CSwapChain12::Present()
{
    Canvas::CFunctionSentinel sentinel("XGfxSwapChain::Present", m_pRenderQueue->GetDevice()->GetLogger(), Canvas::LogLevel::Debug);
    try
    {
        std::unique_lock<std::mutex> Lock(m_mutex);

        // Queue the Present
        UINT presentFlags = 0;
        if (m_TearingSupported)
            presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
        ThrowFailedHResult(m_pSwapChain->Present(0, presentFlags));

        // Get the new back buffer
        CComPtr<ID3D12Resource> pBackBuffer;
        UINT bbindex = m_pSwapChain->GetCurrentBackBufferIndex();
        ThrowFailedHResult(m_pSwapChain->GetBuffer(bbindex, IID_PPV_ARGS(&pBackBuffer)));
        m_pSurface->Rename(pBackBuffer);

        // Initialize committed layout state: new back buffer starts in COMMON layout by spec
        m_pRenderQueue->GetDevice()->InitializeTextureLayout(pBackBuffer, D3D12_BARRIER_LAYOUT_COMMON);

        // Increment the swapchain fence value and queue a signal
        m_FenceValue++;
        ID3D12CommandQueue *pCommandQueue = m_pRenderQueue->GetD3DCommandQueue();
        pCommandQueue->Signal(m_pFence, m_FenceValue);
    }
    catch (_com_error &e)
    {
        sentinel.SetResultCode(ResultFromHRESULT(e.Error()));
        return ResultFromHRESULT(e.Error());
    }

    return Gem::Result::Success;
}

GEMMETHODIMP CSwapChain12::GetSurface(Canvas::XGfxSurface **ppSurface)
{
    return m_pSurface->QueryInterface(ppSurface);
}

GEMMETHODIMP CSwapChain12::WaitForLastPresent()
{
    std::unique_lock<std::mutex> Lock(m_mutex);
    HANDLE hEvent = CreateEvent(nullptr, 0, 0, nullptr);
    m_pFence->SetEventOnCompletion(m_FenceValue, hEvent);
    WaitForSingleObject(hEvent, INFINITE);
    CloseHandle(hEvent);
    return Gem::Result::Success;
}