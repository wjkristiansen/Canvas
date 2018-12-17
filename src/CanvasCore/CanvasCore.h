//================================================================================================
// CanvasCore
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
namespace std
{
    template<>
    class hash<InterfaceId>
    {
    public:
        size_t operator()(InterfaceId iid) const
        {
            return static_cast<size_t>(iid);
        }
    };
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
    case ObjectType::ModelInstance:
        return L"ModelInstance";
    case ObjectType::Light:
        return L"Light";
    }
    return L"<invalid>";
}

//------------------------------------------------------------------------------------------------
class CanvasError
{
    Result m_result;
public:
    operator CanvasError() = delete;
    CanvasError(Result result) :
        m_result(result) {}

    Result Result() const { return m_result; }
};

//------------------------------------------------------------------------------------------------
inline void ThrowFailure(Result result)
{
    if (Failed(result))
    {
        throw(CanvasError(result));
    }
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

    CANVASMETHOD(GetNamedObject)(_In_z_ PCWSTR szName, InterfaceId iid, _Outptr_ void **ppObj)
    {
        auto it = m_ObjectNames.find(szName);
        if (it != m_ObjectNames.end())
        {
            return it->second->QueryInterface(iid, ppObj);
        }
        return Result::NotFound;
    }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj);
    CANVASMETHOD(CreateScene)(InterfaceId iid, _Outptr_ void **ppObj) final;
    CANVASMETHOD(CreateObject)(ObjectType type, InterfaceId iid, _Outptr_ void **ppObj, PCWSTR szName = nullptr) final;

    void ReportObjectLeaks();
};