//================================================================================================
// Generic
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
extern std::unordered_set<typename XGeneric *> g_OutstandingGenerics;

//------------------------------------------------------------------------------------------------
template<class _Base>
class CGeneric : public _Base
{
    ULONG m_RefCount = 0;

public:
    template<typename... Arguments>
    CGeneric(Arguments&&... args) : _Base(args ...)
    {
        try
        {
            g_OutstandingGenerics.insert(this); // throw(std::bad_alloc)
        }
        catch (std::bad_alloc &)
        {
            // Drop tracking of object...
        }
    }

    ~CGeneric()
    {
        g_OutstandingGenerics.erase(this);
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

        if (InterfaceId::XGeneric == iid)
        {
            *ppObj = reinterpret_cast<XGeneric *>(this);
            AddRef();
            return Result::Success;
        }

        return _Base::InternalQueryInterface(iid, ppObj);
    }
};

//------------------------------------------------------------------------------------------------
template<class _Base>
class CInnerGeneric :
    public _Base
{
public:
    template<typename... Arguments>
    CInnerGeneric(_In_ XGeneric *pOuterGeneric, Arguments... params) :
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
