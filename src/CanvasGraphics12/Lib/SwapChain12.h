//================================================================================================
// Context
//================================================================================================

#pragma once

#include "Surface12.h"

//------------------------------------------------------------------------------------------------
class CSwapChain :
    public Canvas::XCanvasGfxSwapChain,
    public Gem::CGenericBase
{
    std::mutex m_mutex;
    CComPtr<IDXGIFactory7> m_pDXGIFactory;
    CComPtr<IDXGISwapChain4> m_pSwapChain;
    CComPtr<ID3D12Fence> m_pFence;
    UINT64 m_FenceValue = 0;
    class CGraphicsContext *m_pContext = nullptr; // weak pointer

public:

    TGemPtr<CSurface> m_pSurface;

    BEGIN_GEM_INTERFACE_MAP(CGenericBase)
        GEM_INTERFACE_ENTRY(XCanvasGfxSwapChain)
    END_GEM_INTERFACE_MAP()

    CSwapChain(HWND hWnd, bool Windowed, class CGraphicsContext *pContext, DXGI_FORMAT Format, UINT NumBuffers);

    GEMMETHOD(GetSurface)(XCanvasGfxSurface **ppSurface) final;
    GEMMETHOD(WaitForLastPresent)() final;

    // Internal functions
    Gem::Result Present();
};

    