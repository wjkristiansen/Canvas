//================================================================================================
// Context
//================================================================================================

#pragma once

#include "Surface12.h"

//------------------------------------------------------------------------------------------------
class CSwapChain12 :
    public Canvas::XGfxSwapChain,
    public Gem::CGenericBase
{
    std::mutex m_mutex;
    CComPtr<IDXGIFactory7> m_pDXGIFactory;
    CComPtr<IDXGISwapChain4> m_pSwapChain;
    CComPtr<ID3D12Fence> m_pFence;
    UINT64 m_FenceValue = 0;
    class CGraphicsContext12 *m_pContext = nullptr; // weak pointer

public:

    TGemPtr<CSurface12> m_pSurface;

    BEGIN_GEM_INTERFACE_MAP(CGenericBase)
        GEM_INTERFACE_ENTRY(XGfxSwapChain)
    END_GEM_INTERFACE_MAP()

    CSwapChain12(HWND hWnd, bool Windowed, class CGraphicsContext12 *pContext, DXGI_FORMAT Format, UINT NumBuffers);

    GEMMETHOD(GetSurface)(XGfxSurface **ppSurface) final;
    GEMMETHOD(WaitForLastPresent)() final;

    // Internal functions
    Gem::Result Present();
};

    