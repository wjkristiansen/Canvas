//================================================================================================
// SwapChain12
//================================================================================================

#include "pch.h"

namespace Canvas
{

CSwapChain12::CSwapChain12(HWND hWnd, bool Windowed, class CGraphicsContext12 *pContext, DXGI_FORMAT Format, UINT NumBuffers) :
    m_pContext(pContext)
{
    // Create the DXGI factory
    CComPtr<IDXGIFactory7> pFactory;
    ThrowFailedHResult(CreateDXGIFactory2(0, IID_PPV_ARGS(&pFactory)));

    CDevice12 *pDevice = pContext->GetDevice();
    ID3D12Device *pD3DDevice = pDevice->GetD3DDevice();

    // Create the swap chain
    CComPtr<IDXGISwapChain1> pSwapChain1;
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = NumBuffers;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
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
    ThrowFailedHResult(pFactory->CreateSwapChainForHwnd(pContext->GetD3DCommandQueue(), hWnd, &swapChainDesc, &swapChainDescFS, nullptr, &pSwapChain1));

    CComPtr<IDXGISwapChain4> pSwapChain4;
    ThrowFailedHResult(pSwapChain1->QueryInterface(&pSwapChain4));

    CComPtr<ID3D12Resource> pBackBuffer;
    UINT bbindex = pSwapChain4->GetCurrentBackBufferIndex();
    ThrowFailedHResult(pSwapChain4->GetBuffer(bbindex, IID_PPV_ARGS(&pBackBuffer)));

    // Craft a D3D12_RESOURCE_DESC to match the swap chain
    Gem::TGemPtr<CSurface12> pSurface = new Gem::TGenericImpl<CSurface12>(pBackBuffer, D3D12_RESOURCE_STATE_COMMON);
        
    CComPtr<ID3D12Fence> pFence;
    ThrowFailedHResult(pD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence)));

    m_pFence.Attach(pFence.Detach());
    m_pDXGIFactory.Attach(pFactory.Detach());
    m_pSwapChain.Attach(pSwapChain4.Detach());
    m_pSurface.Attach(pSurface.Detach());
}

Gem::Result CSwapChain12::Present()
{
    CFunctionSentinel Sentinel(CInstance12::GetSingleton()->Logger(), "XGfxSwapChain::Present", QLog::Level::Debug);
    try
    {
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
        ID3D12CommandQueue *pCommandQueue = m_pContext->GetD3DCommandQueue();
        pCommandQueue->Signal(m_pFence, m_FenceValue);
    }
    catch (_com_error &e)
    {
        Sentinel.SetResultCode(Gem::GemResult(e.Error()));
        return Gem::GemResult(e.Error());
    }

    return Gem::Result::Success;
}

GEMMETHODIMP CSwapChain12::GetSurface(XGfxSurface **ppSurface)
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

}
