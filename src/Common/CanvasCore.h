//================================================================================================
// CanvasCore
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CTimeline
{
public:
    enum class Mode
    {
        Limited,
        Repeating,
        Reversing,
    };

protected:
    double m_BaseTime;
    float m_Duration;
    Mode m_Mode;

public:
    float Evaluate(double ActualTime)
    {
        float Delta = float(ActualTime - m_BaseTime);
        switch (m_Mode)
        {
        case Mode::Limited:
            return min(Delta, m_Duration);
            break;

        case Mode::Repeating:
            return std::fmod(Delta, m_Duration);
            break;

        case Mode::Reversing:
            return m_Duration - std::abs(std::fmod(Delta, m_Duration * 2) - m_Duration);
            break;
        }
    }
};

template<class _DataType>
class CConstantFunction
{
    const _DataType m_c;
    
public:
    CConstantFunction(float c) :
        m_c(c) {}

    _DataType Evaluate() const { return m_c; }
};

template<class _DataType>
class CMidpointFunction
{
    _DataType Evaluate(_DataType x, _DataType y) const { return (x + y) / 2; }
};

template<class _DataType>
class CKeyedCurveFunction
{
    _DataType Evaluate(_DataType t) { return 0; }
};

//------------------------------------------------------------------------------------------------
template<class _DataType>
class CDataSource
{
public:
    virtual _DataType Evaluate() const = 0;
};

template<class _DataType>
class CMidpointFunctionDataSource : public CDataSource<_DataType>
{
    CDataSource<_DataType> *m_pSourceA = nullptr;
    CDataSource<_DataType> *m_pSourceB = nullptr;

public:
    CMidpointFunctionDataSource(CDataSource<_DataType> *pSourceA, CDataSource<_DataType> *pSourceB) :
        m_pSourceA(pSourceA),
        m_pSourceB(pSourceB)
    {}

    virtual _DataType Evaluate() const final
    {
        return m_pSourceA->Evaluate() + m_pSourceB->Evaluate();
    }
};

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

using CanvasLogOutput = SlimLog::CLogOutputBase;

//------------------------------------------------------------------------------------------------
class CCanvasLogger : 
    public SlimLog::TLogger<CanvasLogOutput>
{
public:
    CCanvasLogger(CanvasLogOutput *pLogOutput) :
        SlimLog::TLogger<CanvasLogOutput>(pLogOutput) {}

    void LogError(PCWSTR szOutput)
    {
        LogOutput<SlimLog::LOG_CATEGORY_ERROR>(L"CANVAS ERROR", szOutput);
    }

    void LogErrorF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        LogOutputVA<SlimLog::LOG_CATEGORY_ERROR>(L"CANVAS ERROR", szOutput, args);
        va_end(args);
    }

    void LogWarning(PCWSTR szOutput)
    {
        LogOutput<SlimLog::LOG_CATEGORY_WARNING>(L"CANVAS WARNING", szOutput);
    }

    void LogWarningF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        LogOutputVA<SlimLog::LOG_CATEGORY_WARNING>(L"CANVAS WARNING", szOutput, args);
        va_end(args);
    }

    void LogMessage(PCWSTR szOutput)
    {
        LogOutput<SlimLog::LOG_CATEGORY_MESSAGE>(L"CANVAS MESSAGE", szOutput);
    }

    void LogMessageF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        LogOutputVA<SlimLog::LOG_CATEGORY_MESSAGE>(L"CANVAS MESSAGE", szOutput, args);
        va_end(args);
    }

    void LogInfo(PCWSTR szOutput)
    {
        LogOutput<SlimLog::LOG_CATEGORY_INFO>(L"CANVAS INFO", szOutput);
    }

    void LogInfoF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        LogOutputVA<SlimLog::LOG_CATEGORY_INFO>(L"CANVAS INFO", szOutput, args);
        va_end(args);
    }
};

//------------------------------------------------------------------------------------------------
class CDefaultLogOutput : public SlimLog::CLogOutputBase
{
    std::mutex m_Mutex;

public:
    CDefaultLogOutput() = default;

    virtual void Output(PCWSTR szHeader, PCWSTR szString)
    {
        std::unique_lock<std::mutex> lock(m_Mutex);

        // Debugger
        OutputDebugStringW(L"[");
        OutputDebugStringW(szHeader);
        OutputDebugStringW(L"] ");
        OutputDebugStringW(szString);
        OutputDebugStringW(L"[END]\n");

        // Console
        wprintf_s(L"%s: %s\n", szHeader, szString);
    }
};

//------------------------------------------------------------------------------------------------
class CCanvas :
    public XCanvas,
    public CGenericBase
{
    std::mutex m_Mutex;
    CCanvasLogger m_Logger;

    CTimer m_FrameTimer;
    UINT64 m_FrameEndTimeLast = 0;
    UINT m_FrameCounter = 0;

public:
    CCanvas(SlimLog::CLogOutputBase *pLogOutput) :
        m_Logger(pLogOutput),
        CGenericBase()
    {}

    ~CCanvas();

    std::map<std::wstring, CObjectName *> m_ObjectNames;
    struct Sentinel {};
    TAutoList<TStaticPtr<CCanvasObjectBase>> m_OutstandingObjects;

    GEMMETHOD_(int, SetLogCategoryMask)(int Mask)
    {
        return m_Logger.SetCategoryMask(Mask);
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

    void ReportObjectLeaks();

    CCanvasLogger &Logger() { return m_Logger; }

public:
    TGemPtr<class CGraphicsDevice> m_pGraphicsDevice;
};

//------------------------------------------------------------------------------------------------
inline CCanvasObjectBase::CCanvasObjectBase(CCanvas *pCanvas) :
    CGenericBase(),
    m_pCanvas(pCanvas),
    m_OutstandingNode(pCanvas->m_OutstandingObjects.GetLast(), this)
{
}