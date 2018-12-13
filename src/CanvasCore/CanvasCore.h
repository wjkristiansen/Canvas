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
inline std::string to_string(ObjectType t)
{
    switch (t)
    {
    case ObjectType::Unknown:
        return "Unknown";
    case ObjectType::Null:
        return "Null";
    case ObjectType::Scene:
        return "Scene";
    case ObjectType::SceneGraphNode:
        return "SceneGraphNode";
    case ObjectType::Transform:
        return "Transform";
    case ObjectType::Camera:
        return "Camera";
    case ObjectType::ModelInstance:
        return "ModelInstance";
    case ObjectType::Light:
        return "Light";
    }
    return "<invalid>";
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

    std::map<std::string, CObjectName *> m_ObjectNames;
    std::unordered_set<typename CCanvasObjectBase *> m_OutstandingObjects;

    CANVASMETHOD(GetNamedObject)(_In_z_ PCSTR szName, InterfaceId iid, _Outptr_ void **ppObj)
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
    CANVASMETHOD(CreateObject)(ObjectType type, InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName = nullptr) final;

    void ReportObjectLeaks();
};