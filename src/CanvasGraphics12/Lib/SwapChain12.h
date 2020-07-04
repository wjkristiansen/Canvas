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

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj)
    {
        if (XCanvasGfxSwapChain::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return CGenericBase::InternalQueryInterface(iid, ppObj);
    }

    CSwapChain(HWND hWnd, bool Windowed, class CGraphicsContext *pContext, DXGI_FORMAT Format, UINT NumBuffers);

    GEMMETHOD(GetSurface)(XCanvasGfxSurface **ppSurface) final;
    GEMMETHOD(WaitForLastPresent)() final;

    // Internal functions
    Gem::Result Present();
};

    