//================================================================================================
// Canvas
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CModule
{
    HMODULE m_hModule = NULL;

public:
    CModule() = default;
    explicit CModule(HMODULE hModule) :
        m_hModule(hModule) {}
    CModule(CModule &&o) :
        m_hModule(std::move(o.m_hModule))
    {
        o.m_hModule = NULL;
    }
    CModule(const CModule &o) = delete;
    ~CModule()
    {
        if (m_hModule)
        {
            FreeLibrary(m_hModule);
        }
    }

    CModule &operator=(CModule &&o)
    {
        m_hModule = o.m_hModule;
        o.m_hModule = NULL;
        return *this;
    }

    CModule &operator=(const CModule &o) = delete;

    HMODULE Get() const { return m_hModule; }
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
    CCanvas(SlimLog::CLogOutputBase *pLogOutput) :
        m_Logger(pLogOutput),
        CGenericBase()
    {}

    ~CCanvas();

    std::map<std::wstring, XGeneric *> m_ObjectNames;
    TAutoList<TStaticPtr<CObjectBase>> m_OutstandingObjects;

    GEMMETHOD_(int, SetLogCategoryMask)(int Mask)
    {
        return m_Logger.SetCategoryMask(Mask);
    }

    GEMMETHOD(GetNamedObject)(_In_z_ PCWSTR szName, Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) _Ret_writes_maybenull_(ppObj)
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
    GEMMETHOD(InternalQueryInterface)(Gem::InterfaceId iid, _Outptr_ void **ppObj);
    GEMMETHOD(CreateScene)(Gem::InterfaceId iid, _Outptr_ void **ppObj) final;
    GEMMETHOD(CreateNullSceneGraphNode)(Gem::InterfaceId iid, _Outptr_ void **ppObj, PCWSTR szName = nullptr) final;

    GEMMETHOD(CreateGraphicsDevice)(PCWSTR szDLLPath, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice) final;
    GEMMETHOD(FrameTick)() final;

    void ReportObjectLeaks();

    CCanvasLogger &Logger() { return m_Logger; }

public:
    TGemPtr<class CGraphicsDevice> m_pGraphicsDevice;
};

typedef Result (*CreateCanvasGraphicsDeviceProc)(_In_ CCanvas *pCanvas, _Outptr_ CGraphicsDevice **pGraphicsDevice, HWND hWnd);
