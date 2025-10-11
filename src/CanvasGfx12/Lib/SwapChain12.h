//================================================================================================
// Context
//================================================================================================

#pragma once

#include "Surface12.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CSwapChain12 :
    public Gem::TGeneric<Canvas::XGfxSwapChain>
{
    std::mutex m_mutex;
    CComPtr<IDXGIFactory7> m_pDXGIFactory;
    CComPtr<IDXGISwapChain4> m_pSwapChain;
    CComPtr<ID3D12Fence> m_pFence;
    UINT64 m_FenceValue = 0;
    class CRenderQueue12 *m_pRenderQueue = nullptr; // weak pointer

public:

    Gem::TGemPtr<CSurface12> m_pSurface;

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxSwapChain)
    END_GEM_INTERFACE_MAP()

    CSwapChain12(HWND hWnd, bool Windowed, class CRenderQueue12 *pRenderQueue, DXGI_FORMAT Format, UINT NumBuffers);

    GEMMETHOD(GetSurface)(XGfxSurface **ppSurface) final;
    GEMMETHOD(WaitForLastPresent)() final;

    // Internal functions
    Gem::Result Present();
};

}