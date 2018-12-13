//================================================================================================
// Canvas
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
class CCanvasObjectBase :
    public XGeneric,
    public CGenericBase
{
public:
    class CCanvas *m_pCanvas = nullptr;

    CANVASMETHOD_(ObjectType, GetType)() const = 0;

    CCanvasObjectBase(CCanvas *pCanvas) :
        m_pCanvas(pCanvas) {}
};

//------------------------------------------------------------------------------------------------
template<ObjectType _Type>
class CCanvasObject
{
    ObjectType GetType() const override { return _Type; }
};

//------------------------------------------------------------------------------------------------
class CCanvas :
    public XCanvas,
    public CGenericBase
{
public:
    CCanvas() = default;
    std::map<std::string, CObjectName *> m_ObjectNames;

    CANVASMETHOD(GetNamedObject)(_In_z_ PCSTR szName, InterfaceId iid, _Outptr_ void **ppObj)
    {
        auto it = m_ObjectNames.find(szName);
        if (it != m_ObjectNames.end())
        {
            return it->second->QueryInterface(iid, ppObj);
        }
        return Result::NotFound;
    }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final;
    CANVASMETHOD(CreateScene)(InterfaceId iid, void **ppScene) final;
    CANVASMETHOD(CreateObject)(ObjectType type, InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName = nullptr) final;
};