//================================================================================================
// Generic
//================================================================================================

#pragma once

#define GEMAPI __stdcall
#define GEMNOTHROW __declspec(nothrow)
#define GEMMETHOD(method) virtual GEMNOTHROW Gem::Result GEMAPI method
#define GEMMETHOD_(retType, method) virtual GEMNOTHROW retType GEMAPI method
#define GEMMETHODIMP Gem::Result
#define GEMMETHODIMP_(retType) retType
#define GEM_INTERFACE struct
#define GEM_INTERFACE_DECLARE(iid) static const Gem::InterfaceId IId = iid

#define GEM_IID_PPV_ARGS(ppObj) \
    std::remove_reference_t<decltype(**ppObj)>::IId, reinterpret_cast<void **>(ppObj)

namespace Gem
{
typedef UINT InterfaceId;

// Forward decl XGeneric
GEM_INTERFACE XGeneric;

//------------------------------------------------------------------------------------------------
_Return_type_success_(return < 0x80000000)
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
    Unavailable,
    Uninitialized,
};

//------------------------------------------------------------------------------------------------
inline PCSTR ResultToString(Result res)
{
    switch (res)
    {
    case Result::Success:
        return "Success";
    case Result::End:
        return "End";
    case Result::Fail:
        return "Fail";
    case Result::InvalidArg:
        return "IncalidArg";
    case Result::NotFound:
        return "NotFound";
    case Result::OutOfMemory:
        return "OutOfMemory";
    case Result::NoInterface:
        return "NoInterface";
    case Result::BadPointer:
        return "BadPointer";
    case Result::NotImplemented:
        return "NotImplemented";
    case Result::Unavailable:
        return "Unavailable";
    case Result::Uninitialized:
        return "Uninitialized";
    }
    return "(Unknown)";
}

//------------------------------------------------------------------------------------------------
inline bool Succeeded(Result result)
{
    return result < Result::Fail;
}

//------------------------------------------------------------------------------------------------
template<class _Type>
class TGemPtr
{
    _Type *m_p = nullptr;

public:
    TGemPtr() = default;
    TGemPtr(_Type *p) :
        m_p(p)
    {
        if (m_p)
        {
            m_p->AddRef();
        }
    }
    TGemPtr(const TGemPtr &o) :
        m_p(o.m_p)
    {
        if (m_p)
        {
            m_p->AddRef();
        }
    }
    TGemPtr(TGemPtr &&o) noexcept :
        m_p(o.m_p)
    {
        o.m_p = nullptr;
    }

    ~TGemPtr()
    {
        if (m_p)
        {
            m_p->Release();
        }
    }

    void Detach()
    {
        m_p = nullptr;
    }

    TGemPtr &operator=(_Type *p)
    {
        if (m_p)
        {
            m_p->Release();
        }

        m_p = p;
        if (p)
        {
            m_p->AddRef();
        }

        return *this;
    }

    TGemPtr &operator=(const TGemPtr &o)
    {
        auto temp = m_p;

        m_p = o.m_p;

        if (temp != m_p)
        {
            if (m_p)
            {
                m_p->AddRef();
            }

            if (temp)
            {
                temp->Release();
            }
        }

        return *this;
    }

    TGemPtr &operator=(TGemPtr &&o) noexcept
    {
        if (m_p != o.m_p)
        {
            auto temp = m_p;

            m_p = o.m_p;

            if (temp)
            {
                temp->Release();
            }
        }
        o.m_p = nullptr;

        return *this;
    }

    _Type **operator&()
    {
        return &m_p;
    }

    _Type &operator*() const
    {
        return *m_p;
    }

    _Type *Get() const { return m_p; }

    operator _Type *() const { return m_p; }
    
    _Type *operator->() const { return m_p; }
};

//------------------------------------------------------------------------------------------------
inline bool Failed(Gem::Result result)
{
    return result >= Gem::Result::Fail;
}

//------------------------------------------------------------------------------------------------
class GemError
{
    Result m_result;
public:
    operator GemError() = delete;
    GemError(Result result) :
        m_result(result) {}

    Result Result() const { return m_result; }
};

//------------------------------------------------------------------------------------------------
inline void ThrowGemError(Result result)
{
    if (Failed(result))
    {
        throw(GemError(result));
    }
}

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

    GEMMETHOD_(ULONG,AddRef)() final
    {
        return InternalAddRef();
    }

    GEMMETHOD_(ULONG, Release)() final
    {
        return InternalRelease();
    }

    ULONG GEMNOTHROW GEMAPI InternalAddRef()
    {
        return InterlockedIncrement(&m_RefCount);
    }

    ULONG GEMNOTHROW GEMAPI InternalRelease()
    {
        auto result = InterlockedDecrement(&m_RefCount);

        if (0UL == result)
        {
            delete(this);
        }

        return result;
    }

    GEMMETHOD(QueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) final
    {
        if (!ppObj)
        {
            return Result::BadPointer;
        }

        return InternalQueryInterface(iid, ppObj);
    }

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) final
    {
        if (XGeneric::IId == iid)
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
    GEMMETHOD_(ULONG,AddRef)() final
    {
        return _Base::m_pOuterGeneric->AddRef();
    }

    // Delegate Release to outer generic
    GEMMETHOD_(ULONG, Release)() final
    {
        return _Base::m_pOuterGeneric->Release();
    }

    // Delegate Query interface to outer generic
    GEMMETHOD(QueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) final
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
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppUnk)
    {
        *ppUnk = nullptr;
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
GEM_INTERFACE XGeneric
{
    GEM_INTERFACE_DECLARE(0xffffffffU);

    GEMMETHOD_(ULONG, AddRef)() = 0;
    GEMMETHOD_(ULONG, Release)() = 0;
    GEMMETHOD(QueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) = 0;

    template<class _XFace>
    Gem::Result QueryInterface(_XFace **ppObj)
    {
        return QueryInterface(_XFace::IId, reinterpret_cast<void **>(ppObj));
    }
};

}
