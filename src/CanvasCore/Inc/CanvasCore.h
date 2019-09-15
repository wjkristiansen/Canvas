//================================================================================================
// CanvasCore
//================================================================================================

#pragma once

#include "Module.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
inline Result HResultToResult(HRESULT hr)
{
    switch (hr)
    {
    case S_OK:
        return Result::Success;

    case E_FAIL:
        return Result::Fail;

    case E_OUTOFMEMORY:
        return Result::OutOfMemory;

    case E_INVALIDARG:
    case DXGI_ERROR_INVALID_CALL:
        return Result::InvalidArg;

    case DXGI_ERROR_DEVICE_REMOVED:
        // BUGBUG: TODO...
        return Result::Fail;

    case E_NOINTERFACE:
        return Result::NoInterface;

    default:
        return Result::Fail;
    }
}

//------------------------------------------------------------------------------------------------
class CCanvas :
    public XCanvas,
    public CGenericBase
{
    std::mutex m_Mutex;
    CCanvasLogger m_Logger;
    CModule m_GraphicsModule;

    CTimer m_FrameTimer;
    UINT64 m_FrameEndTimeLast = 0;
    UINT m_FrameCounter = 0;

public:
    CCanvas(QLog::CLogClient *pLogClient) :
        m_Logger(pLogClient),
        CGenericBase()
    {}

    ~CCanvas();

    std::map<std::string, XGeneric *> m_ObjectNames;
    TAutoList<TStaticPtr<CObjectBase>> m_OutstandingObjects;

    GEMMETHOD(GetNamedObject)(_In_z_ PCSTR szName, Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj)
    {
        std::unique_lock<std::mutex> Lock(m_Mutex);
        auto it = m_ObjectNames.find(szName);
        if (it != m_ObjectNames.end())
        {
            return it->second->QueryInterface(iid, ppObj);
        }
        
        *ppObj = nullptr;

        return Result::NotFound;
    }

    // XCanvas methods
    GEMMETHOD(InternalQueryInterface)(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj);
    GEMMETHOD(CreateScene)(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) final;
    GEMMETHOD(CreateNullSceneGraphNode)(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj, PCSTR szName = nullptr) final;
    GEMMETHOD(CreateCameraNode)(_In_ const ModelData::CAMERA_DATA *pCameraData, _Outptr_result_nullonfailure_ XCamera **ppCamera, _In_z_ PCSTR szName = nullptr);
    GEMMETHOD(CreateLightNode)(const ModelData::LIGHT_DATA *pLightData, _Outptr_result_nullonfailure_ XLight **ppLight, _In_z_ PCSTR szName = nullptr);

    GEMMETHOD(CreateGraphicsDevice)(PCSTR szDLLPath, HWND hWnd, _Outptr_opt_result_nullonfailure_ XGraphicsDevice **ppGraphicsDevice) final;
    GEMMETHOD(FrameTick)() final;

    void ReportObjectLeaks();

    CCanvasLogger &Logger() { return m_Logger; }

public:
    TGemPtr<class Graphics::CDevice> m_pGraphicsDevice;
};

typedef Result (*CreateCanvasGraphicsDeviceProc)(_In_ CCanvas *pCanvas, _Outptr_opt_result_nullonfailure_ Canvas::Graphics::CDevice **pGraphicsDevice, HWND hWnd);
