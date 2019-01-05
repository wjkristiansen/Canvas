//================================================================================================
// Canvas
//================================================================================================

#pragma once

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

    GEMMETHOD(SetupGraphics)(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd) final;
    GEMMETHOD(FrameTick)() final;
    GEMMETHOD(CreateMesh)(const MESH_DATA *pMeshData) final;

    Result SetupD3D12(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd);

    void ReportObjectLeaks();

public:
    TGemPtr<CGraphicsDevice> m_pGraphicsDevice;
};