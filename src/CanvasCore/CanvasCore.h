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

    CCanvasObjectBase(CCanvas *pCanvas) :
        m_pCanvas(pCanvas) {}
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

    CANVASMETHOD(CreateTransformObject)(InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName = nullptr) final;
    CANVASMETHOD(CreateCameraObject)(_In_z_ InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName = nullptr) final;
    CANVASMETHOD(CreateLightObject)(_In_z_ InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName = nullptr) final;
    CANVASMETHOD(CreateModelInstanceObject)(_In_z_ InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName = nullptr) final;
    CANVASMETHOD(CreateCustomObject)(_In_z_ InterfaceId *pInnerInterfaces, UINT numInnerInterfaces, _Outptr_ void **ppObj, PCSTR szName = nullptr) final;
};