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
inline std::wstring to_string(ObjectType t)
{
    switch (t)
    {
    case ObjectType::Unknown:
        return L"Unknown";
    case ObjectType::Null:
        return L"Null";
    case ObjectType::Scene:
        return L"Scene";
    case ObjectType::SceneGraphNode:
        return L"SceneGraphNode";
    case ObjectType::Transform:
        return L"Transform";
    case ObjectType::Camera:
        return L"Camera";
    case ObjectType::MeshInstance:
        return L"MeshInstance";
    case ObjectType::Light:
        return L"Light";
    }
    return L"<invalid>";
}

//------------------------------------------------------------------------------------------------
class CCanvas :
    public XCanvas,
    public CGenericBase
{
public:
    CCanvas() = default;
    ~CCanvas();

    std::map<std::wstring, CObjectName *> m_ObjectNames;
    std::unordered_set<typename CCanvasObjectBase *> m_OutstandingObjects;

    GEMMETHOD(GetNamedObject)(_In_z_ PCWSTR szName, InterfaceId iid, _Outptr_ void **ppObj)
    {
        auto it = m_ObjectNames.find(szName);
        if (it != m_ObjectNames.end())
        {
            return it->second->QueryInterface(iid, ppObj);
        }
        return Result::NotFound;
    }

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj);
    GEMMETHOD(CreateScene)(InterfaceId iid, _Outptr_ void **ppObj) final;
    GEMMETHOD(CreateObject)(ObjectType type, InterfaceId iid, _Outptr_ void **ppObj, PCWSTR szName = nullptr) final;

    GEMMETHOD(SetupGraphics)(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice) final;
    GEMMETHOD(FrameTick)() final;

    Result SetupD3D12(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice);

    void ReportObjectLeaks();

public:
    TGemPtr<CGraphicsDevice> m_pGraphicsDevice;
};