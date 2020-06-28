//================================================================================================
// Canvas
//================================================================================================

#pragma once

#include "Module.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
class CCanvas :
    public XCanvas,
    public CGenericBase
{
    std::mutex m_Mutex;
    QLog::CBasicLogger m_Logger;
    CModule m_GraphicsModule;

    CTimer m_FrameTimer;
    UINT64 m_FrameEndTimeLast = 0;
    UINT m_FrameCounter = 0;

public:
    CCanvas(QLog::CLogClient *pLogClient) :
        m_Logger(pLogClient, "CANVAS"),
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

    GEMMETHOD(CreateGfxDevice)(PCSTR szDLLPath, _Outptr_opt_result_nullonfailure_ XCanvasGfxDevice **ppGraphicsDevice) final;
    GEMMETHOD(FrameTick)() final;

    void ReportObjectLeaks();

    QLog::CBasicLogger &Logger() { return m_Logger; }

public:
    TGemPtr<XCanvasGfxDevice> m_pGraphicsDevice;
};

typedef Result (*CreateCanvasGraphicsDeviceProc)(_Outptr_opt_result_nullonfailure_ XCanvasGfxDevice **pGraphicsDevice, QLog::CLogClient *pLogClient);
