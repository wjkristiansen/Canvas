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
        CComPtr<IDXGIFactory2> pFactory;
        ThrowFailedHResult(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory)));

        // Create the swap chain
        CComPtr<IDXGISwapChain1> pSwapChain;
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = 3;
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
        ThrowFailedHResult(pFactory->CreateSwapChainForHwnd(pCommandQueue, hWnd, &swapChainDesc, &swapChainDescFS, nullptr, &pSwapChain));

        m_pD3DDevice = pDevice;
        m_pDXGIFactory = pFactory;
        m_pDirectCommandQueue = pCommandQueue;
        m_pSwapChain = pSwapChain;
    }
    catch (_com_error &e)
    {
        return HResultToResult(e.Error());
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
Result CGraphicsDevice12::Present()
{
    try
    {
        ThrowFailedHResult(m_pSwapChain->Present(0, 0));
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
