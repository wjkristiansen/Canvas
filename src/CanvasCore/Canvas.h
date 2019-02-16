//================================================================================================
// Canvas
//================================================================================================

#pragma once

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
    LOG_OUTPUT_LEVEL m_MaxLogOutputLevel;
    CLogOutput *m_pLogOutput;

public:
    CCanvas(CLogOutput *pLogOutput) :
        CGenericBase(),
        m_MaxLogOutputLevel(LOG_OUTPUT_LEVEL_MESSAGE),
        m_pLogOutput(pLogOutput)
    {}

    ~CCanvas();

    std::map<std::wstring, CObjectName *> m_ObjectNames;
    struct Sentinel {};
    TAutoList<TStaticPtr<CCanvasObjectBase>> m_OutstandingObjects;

    GEMMETHOD_(void, SetMaxOutputLevel)(LOG_OUTPUT_LEVEL Level)
    {
        std::unique_lock<std::mutex> Lock(m_Mutex);
        m_MaxLogOutputLevel = Level;
    }

    GEMMETHOD(GetNamedObject)(_In_z_ PCWSTR szName, Gem::InterfaceId iid, _Outptr_ void **ppObj)
    {
        std::unique_lock<std::mutex> Lock(m_Mutex);
        auto it = m_ObjectNames.find(szName);
        if (it != m_ObjectNames.end())
        {
            return it->second->QueryInterface(iid, ppObj);
        }
        return Result::NotFound;
    }

    // XCanvas methods
    GEMMETHOD(InternalQueryInterface)(Gem::InterfaceId iid, _Outptr_ void **ppObj);
    GEMMETHOD(CreateScene)(Gem::InterfaceId iid, _Outptr_ void **ppObj) final;
    GEMMETHOD(CreateSceneGraphNode)(Gem::InterfaceId iid, _Outptr_ void **ppObj, PCWSTR szName = nullptr) final;

    GEMMETHOD(CreateGraphicsDevice)(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice) final;
    GEMMETHOD(FrameTick)() final;

    Result SetupD3D12(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice);

    void WriteToLog(LOG_OUTPUT_LEVEL Level, PCWSTR szLogString);

    void ReportObjectLeaks();

public:
    TGemPtr<class CGraphicsDevice> m_pGraphicsDevice;
};

//------------------------------------------------------------------------------------------------
inline CCanvasObjectBase::CCanvasObjectBase(CCanvas *pCanvas) :
    CGenericBase(),
    m_OutstandingNode(pCanvas->m_OutstandingObjects.GetLast(), this)
{
}