//================================================================================================
// Canvas
//================================================================================================

#pragma once

#include "Gem.hpp"
#include "Timer.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CCanvas :
    public Gem::CGenericBase,
    public XCanvas
{
    std::mutex m_Mutex;
    std::shared_ptr<QLog::Logger> m_Logger;
    wil::unique_hmodule m_GraphicsModule;

    CTimer m_FrameTimer;
    UINT64 m_FrameEndTimeLast = 0;
    UINT m_FrameCounter = 0;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XCanvas)
    END_GEM_INTERFACE_MAP()

    CCanvas(std::shared_ptr<QLog::Logger> pLogger) :
        m_Logger(pLogger),
        CGenericBase()
    {}

    ~CCanvas();

    // XCanvas methods
    GEMMETHOD(InitCanvasGfx)(PCSTR szDLLPath, _Outptr_result_nullonfailure_ XGfxInstance **ppCanvasGfx) final;
    GEMMETHOD(FrameTick)() final;

    GEMMETHOD(CreateScene)(XScene **ppScene) final;
    GEMMETHOD(CreateSceneGraphNode)(XSceneGraphNode **ppNode) final;
    GEMMETHOD(CreateTransform)(XTransform **ppTransform) final;
    GEMMETHOD(CreateCamera)(const ModelData::CAMERA_DATA &cameraData, XCamera **ppCamera) final;
    GEMMETHOD(CreateLight)(const ModelData::LIGHT_DATA &lightData, XLight **ppLight) final;

    std::shared_ptr<QLog::Logger> Logger() { return m_Logger; }

public:
    Gem::TGemPtr<XGfxInstance> m_pCanvasGfx;
};

//------------------------------------------------------------------------------------------------
template<class _Base>
class TCanvasElement :
    public _Base
{
    std::string m_Name;
    CCanvas *m_pCanvas;

public:
    TCanvasElement(CCanvas *pCanvas) :
        m_pCanvas(pCanvas)
    {
    }

    // XCanvasElement methods
    GEMMETHOD_(PCSTR, GetName)() { return m_Name.c_str(); }
    GEMMETHOD_(void, SetName)(PCSTR szName) { m_Name = szName; }
    GEMMETHOD_(XCanvas *, GetCanvas()) { return m_pCanvas; }

public:
    CCanvas *GetCanvasImpl() { return m_pCanvas; }
};

typedef Gem::Result (*CreateCanvasGfxProc)(_Outptr_result_nullonfailure_ XGfxInstance **pGraphicsGfx, std::shared_ptr<QLog::Logger> pLogger) noexcept;

}