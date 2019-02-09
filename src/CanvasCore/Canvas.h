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
class CLogger : public XCanvas
{
    LOG_OUTPUT_LEVEL m_MaxLogOutput;
    LogOutputProc m_LogOutputProc;

public:
    CLogger(LogOutputProc OutputProc) :
        m_MaxLogOutput(LOG_OUTPUT_LEVEL_MESSAGE),
        m_LogOutputProc(OutputProc)
    {
        if (m_LogOutputProc == nullptr)
        {
            m_LogOutputProc = DefaultOutputProc;
        }
    }

    static void DefaultOutputProc(LOG_OUTPUT_LEVEL Level, PCWSTR szSTring);
    
    // XLogger methods
    GEMMETHOD_(void, SetMaxOutputLevel)(LOG_OUTPUT_LEVEL Level) { m_MaxLogOutput = Level; }
    GEMMETHOD_(void, WriteToLog)(LOG_OUTPUT_LEVEL Level, PCWSTR szString);
    GEMMETHOD_(void, SetLogOutputProc)(LogOutputProc OutputProc) { m_LogOutputProc = OutputProc; }
};

//------------------------------------------------------------------------------------------------
class CCanvas :
    public CLogger,
    public CGenericBase
{
public:
    CCanvas(LogOutputProc OutputProc) :
        CGenericBase(),
        CLogger(OutputProc)
    {}

    ~CCanvas();

    std::map<std::wstring, CObjectName *> m_ObjectNames;
    struct Sentinel {};
    TAutoList<TStaticPtr<CCanvasObjectBase>> m_OutstandingObjects;

    GEMMETHOD(GetNamedObject)(_In_z_ PCWSTR szName, Gem::InterfaceId iid, _Outptr_ void **ppObj)
    {
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