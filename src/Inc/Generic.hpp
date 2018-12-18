//================================================================================================
// Generic
//================================================================================================

#pragma once

#define GOMAPI __stdcall
#define GOMNOTHROW __declspec(nothrow)
#define GOMMETHOD(method) virtual GOMNOTHROW GOM::Result GOMAPI method
#define GOMMETHOD_(retType, method) virtual GOMNOTHROW retType GOMAPI method
#define GOMMETHODIMP GOM::Result
#define GOMMETHODIMP_(retType) retType
#define GOM_INTERFACE struct
#define GOM_INTERFACE_DECLARE(iid) static const GOM::InterfaceId IId = iid

#define GOM_IID_PPV_ARGS(ppObj) \
    std::remove_reference_t<decltype(**ppObj)>::IId, reinterpret_cast<void **>(ppObj)

namespace GOM
{
typedef UINT InterfaceId;

// Forward decl XGeneric
GOM_INTERFACE XGeneric;

//------------------------------------------------------------------------------------------------
enum class Result : UINT32
{
    Success = 0,
    End = 1,
    Fail = 0x80000000, // Must be first failure
    InvalidArg,
    NotFound,
    OutOfMemory,
    NoInterface,
    BadPointer,
    NotImplemented,
    DuplicateKey,
    Uninitialized,
};

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

    GOMMETHOD_(ULONG,AddRef)() final
    {
        return InternalAddRef();
    }

    GOMMETHOD_(ULONG, Release)() final
    {
        return InternalRelease();
    }

    ULONG GOMNOTHROW GOMAPI InternalAddRef()
    {
        return InterlockedIncrement(&m_RefCount);
    }

    ULONG GOMNOTHROW GOMAPI InternalRelease()
    {
        auto result = InterlockedDecrement(&m_RefCount);

        if (XGeneric::IId == result)
        {
            delete(this);
        }

        return result;
    }

    GOMMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (!ppObj)
        {
            return Result::BadPointer;
        }

        *ppObj = nullptr;

        return InternalQueryInterface(iid, ppObj);
    }

    GOMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (XGeneric::IId == iid)
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
    GOMMETHOD_(ULONG,AddRef)() final
    {
        return _Base::m_pOuterGeneric->AddRef();
    }

    // Delegate Release to outer generic
    GOMMETHOD_(ULONG, Release)() final
    {
        return _Base::m_pOuterGeneric->Release();
    }

    // Delegate Query interface to outer generic
    GOMMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
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
    GOMMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk)
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
    
//------------------------------------------------------------------------------------------------
GOM_INTERFACE XGeneric
{
    GOM_INTERFACE_DECLARE(0xffffffffU);

    GOMMETHOD_(ULONG, AddRef)() = 0;
    GOMMETHOD_(ULONG, Release)() = 0;
    GOMMETHOD(QueryInterface)(InterfaceId iid, void **ppObj) = 0;

    template<class _XFace>
    GOM::Result QueryInterface(_XFace **ppObj)
    {
        return QueryInterface(_XFace::IId, reinterpret_cast<void **>(ppObj));
    }
};

}
