//================================================================================================
// Canvas
//================================================================================================

#pragma once

using namespace Canvas;

//------------------------------------------------------------------------------------------------
class CCanvas :
    public XCanvas,
    public CGenericBase
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
    GEMMETHOD(CreateScene)(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) final;
    GEMMETHOD(CreateNullSceneGraphNode)(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj, PCSTR szName = nullptr) final;
    GEMMETHOD(CreateCameraNode)(_In_ const ModelData::CAMERA_DATA *pCameraData, _Outptr_result_nullonfailure_ XCamera **ppCamera, _In_z_ PCSTR szName = nullptr);
    GEMMETHOD(CreateLightNode)(const ModelData::LIGHT_DATA *pLightData, _Outptr_result_nullonfailure_ XLight **ppLight, _In_z_ PCSTR szName = nullptr);

    GEMMETHOD(InitCanvasGfx)(PCSTR szDLLPath, _Outptr_result_nullonfailure_ XGfxInstance **ppCanvasGfx) final;
    GEMMETHOD(FrameTick)() final;

    void ReportObjectLeaks();

    std::shared_ptr<QLog::Logger> Logger() { return m_Logger; }

public:
    TGemPtr<XGfxInstance> m_pCanvasGfx;
};

typedef Result (*CreateCanvasGfxProc)(_Outptr_result_nullonfailure_ XGfxInstance **pGraphicsGfx, std::shared_ptr<QLog::Logger> pLogger) noexcept;
