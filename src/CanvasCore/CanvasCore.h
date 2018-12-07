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

    ULONG CANVASNOTHROW CANVASAPI InternalAddRef()
    {
        return InterlockedIncrement(&m_RefCount);
    }

    ULONG CANVASNOTHROW CANVASAPI InternalRelease()
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
template<class _Base, InterfaceId IId>
class CInnerGeneric :
    public _Base
{
public:
    XGeneric *m_pOuterGeneric = 0; // weak pointer

    CInnerGeneric(_In_ XGeneric *pOuterGeneric) :
        m_pOuterGeneric(pOuterGeneric)
    {
    }

    // Forward AddRef to outer generic
    CANVASMETHOD_(ULONG,AddRef)() final
    {
        return m_pOuterGeneric->AddRef();
    }

    // Forward Release to outer generic
    CANVASMETHOD_(ULONG, Release)() final
    {
        return m_pOuterGeneric->Release();
    }

    // Forward Query interface to outer generic
    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        return m_pOuterGeneric->QueryInterface(iid, ppObj);
    }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return Result::NoInterface;
    }
};


//------------------------------------------------------------------------------------------------
// Custom interfaces must derive from CGenericBase
class CGenericBase
{
public:
    virtual ~CGenericBase() = default;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        return Result::NoInterface;
    }
};

//------------------------------------------------------------------------------------------------
class CCustomObject :
    public XGeneric,
    public CGenericBase
{
public:
    // For now just create a vector of inner element pointers.  Consider
    // in the future allocating a contiguous chunk of memory for all
    // elements (including the outer CObject interface) and using
    // placement-new to allocate the whole object
    std::vector<std::unique_ptr<CGenericBase>> m_InnerElements;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk)
    {
        for (auto &pElement : m_InnerElements)
        {
            Result res = pElement->InternalQueryInterface(iid, ppUnk);
            if (Result::NoInterface != res)
            {
                return res;
            }
        }

        return Result::NoInterface;
    }
};
