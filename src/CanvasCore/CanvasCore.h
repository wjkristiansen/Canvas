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
template<class _Base>
class CGeneric : public _Base
{
    ULONG m_RefCount = 0;

public:
    template<typename... Arguments>
    CGeneric(Arguments&&... args) : _Base(args ...)
    {
    }

    CANVASMETHOD_(ULONG,AddRef)() final
    {
        return InternalAddRef();
    }

    CANVASMETHOD_(ULONG, Release)() final
    {
        return InternalRelease();
    }

    ULONG CANVASNOTHROW InternalAddRef()
    {
        return InterlockedIncrement(&m_RefCount);
    }

    ULONG CANVASNOTHROW InternalRelease()
    {
        auto result = InterlockedDecrement(&m_RefCount);

        if (0 == result)
        {
            delete(this);
        }

        return result;
    }

    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) override
    {
        if (!ppObj)
        {
            return Result::BadPointer;
        }

        *ppObj = nullptr;

        return InternalQueryInterface(iid, ppObj);
    }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        *ppObj = nullptr;
        switch (iid)
        {
        case InterfaceId::XGeneric:
            *ppObj = this;
            AddRef();
            break;

        default:
            return __super::InternalQueryInterface(iid, ppObj);
        }

        return Result::Success;
    }
};

//------------------------------------------------------------------------------------------------
class CObject :
    public XGeneric,
    public CGenericBase
{
    using ElementMapType = std::unordered_map<InterfaceId, std::unique_ptr<CGenericBase>>;
    ElementMapType m_Elements;

public:
    static Result CANVASAPI Create(OBJECT_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppObj);
    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk);
};
