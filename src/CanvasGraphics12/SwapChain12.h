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
    CComPtr<IDXGIFactory7> m_pDXGIFactory;
    CComPtr<IDXGISwapChain1> m_pSwapChain;
    CComPtr<ID3D12Fence> m_pFence;
    UINT64 m_FenceValue = 0;
    TGemPtr<CSurface> m_pSurface;
    ID3D12CommandQueue *m_pCommandQueue = nullptr; // weak

public:

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

    CSwapChain(HWND hWnd, bool Windowed, ID3D12Device *pDevice, ID3D12CommandQueue *pCommandQueue);

    GEMMETHOD(Present)() final;
    GEMMETHOD(GetSurface)(XCanvasGfxSurface **ppSurface) final;
};

    