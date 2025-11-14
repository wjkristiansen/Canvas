//================================================================================================
// Context
//================================================================================================

#pragma once

#include "Surface12.h"
#include "CanvasGfx12.h"

// Forward declarations for task graph support
namespace Canvas { using TaskID = uint64_t; }

//------------------------------------------------------------------------------------------------
class CSwapChain12 :
    public TGfxElement<Canvas::XGfxSwapChain>
{
    friend class CRenderQueue12;  // Allow render queue to access task tracking
    
    std::mutex m_mutex;
    CComPtr<IDXGIFactory7> m_pDXGIFactory;
    CComPtr<IDXGISwapChain4> m_pSwapChain;
    CComPtr<ID3D12Fence> m_pFence;
    UINT64 m_FenceValue = 0;
    class CRenderQueue12 *m_pRenderQueue = nullptr; // weak pointer
    // DXGI frame pacing
    HANDLE m_hFrameLatencyWaitableObject = nullptr;
    bool m_TearingSupported = false;
    
    // Frame dependency tracking for task-based rendering
    Canvas::TaskID m_LastFramePresentTask = 0;  // 0 == NullTaskID
    bool m_BackBufferModified = false;          // True if back buffer received any GPU writes this recording phase

public:

    Gem::TGemPtr<CSurface12> m_pSurface;

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxSwapChain)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CSwapChain12(Canvas::XCanvas* pCanvas, HWND hWnd, bool Windowed, class CRenderQueue12 *pRenderQueue, DXGI_FORMAT Format, UINT NumBuffers, PCSTR name = nullptr);

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    GEMMETHOD(GetSurface)(Canvas::XGfxSurface **ppSurface) final;
    GEMMETHOD(WaitForLastPresent)() final;

    // Internal functions
    Gem::Result Present();
    void WaitForFrameLatency()
    {
        if (m_hFrameLatencyWaitableObject)
        {
            WaitForSingleObject(m_hFrameLatencyWaitableObject, INFINITE);
        }
    }
};
