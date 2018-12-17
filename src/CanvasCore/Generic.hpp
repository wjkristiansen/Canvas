//================================================================================================
// Generic
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
template<class _Base>
class TGeneric : public _Base
{
    ULONG m_RefCount = 0;

public:
    template<typename... Arguments>
    TGeneric(Arguments&&... args) : _Base(args ...)
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

    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
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
        if (InterfaceId::XGeneric == iid)
        {
            *ppObj = reinterpret_cast<XGeneric *>(this);
            AddRef();
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

//------------------------------------------------------------------------------------------------
template<class _Base>
class TInnerGeneric :
    public _Base
{
public:
    template<typename... Arguments>
    TInnerGeneric(_In_ XGeneric *pOuterGeneric, Arguments... params) :
        _Base(pOuterGeneric, params...)
    {
    }

    // Delegate AddRef to outer generic
    CANVASMETHOD_(ULONG,AddRef)() final
    {
        return _Base::m_pOuterGeneric->AddRef();
    }

    // Delegate Release to outer generic
    CANVASMETHOD_(ULONG, Release)() final
    {
        return _Base::m_pOuterGeneric->Release();
    }

    // Delegate Query interface to outer generic
    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        return _Base::m_pOuterGeneric->QueryInterface(iid, ppObj);
    }
};

//------------------------------------------------------------------------------------------------
// Custom interfaces must derive from CGenericBase
class CGenericBase
{
public:
    virtual ~CGenericBase() = default;
    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk)
    {
        return Result::NoInterface;
    }
};

//------------------------------------------------------------------------------------------------
class CInnerGenericBase :
    public CGenericBase
{
public:
    XGeneric *m_pOuterGeneric = nullptr; // weak pointer
    CInnerGenericBase(XGeneric *pOuterGeneric) :
        m_pOuterGeneric(pOuterGeneric) {}
};

extern void ReportGenericLeaks();
