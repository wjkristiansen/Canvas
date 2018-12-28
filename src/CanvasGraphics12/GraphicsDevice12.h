//================================================================================================
// GraphicsDevice12
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CGraphicsDevice12 :
    public CGraphicsDevice
{
public:
    CComPtr<ID3D12Device5> m_pD3DDevice;
    CComPtr<ID3D12CommandQueue> m_pDirectCommandQueue;
    CComPtr<IDXGIFactory7> m_pDXGIFactory;
    CComPtr<IDXGISwapChain4> m_pSwapChain;

    CGraphicsDevice12();

    virtual Result Initialize(HWND hWnd, bool Windowed);
    virtual Result Present();
};
