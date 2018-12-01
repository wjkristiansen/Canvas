//================================================================================================
// Canvas
//================================================================================================

#pragma once

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
class CCanvasObjectBase
{
public:
    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (!ppObj)
        {
            return Result::BadPointer;
        }

        *ppObj = nullptr;
        return Result::NoInterface;
    }
};

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

    CANVASMETHOD_(ULONG,AddRef)()
    {
        return InterlockedIncrement(&m_RefCount);
    }

    CANVASMETHOD_(ULONG, Release)()
    {
        auto result = InterlockedDecrement(&m_RefCount);

        if (0 == result)
        {
            delete(this);
        }

        return result;
    }

    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        *ppObj = nullptr;
        switch (iid)
        {
        case InterfaceId::IGeneric:
            *ppObj = this;
            AddRef();
            break;

        default:
            return _Base::QueryInterface(iid, ppObj);
        }

        return Result::Success;
    }
};

//------------------------------------------------------------------------------------------------
template<class _IFace>
class CInnerGeneric :
    public _IFace
{
public:
    IGeneric *m_pOuterGeneric = 0; // weak pointer

    CInnerGeneric(_In_ IGeneric *pOuterGeneric) :
        m_pOuterGeneric(pOuterGeneric)
    {
        if (!pOuterGeneric)
        {
            ThrowFailure(Result::BadPointer);
        }
    }

    CANVASMETHOD_(ULONG,AddRef)()
    {
        return m_pOuterGeneric->AddRef();
    }

    CANVASMETHOD_(ULONG, Release)()
    {
        return m_pOuterGeneric->Release();
    }

    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (IGeneric::IId == iid)
        {
            return m_pOuterGeneric->QueryInterface(iid, ppObj);
        }

        *ppObj = nullptr;

        if (!ppObj)
        {
            return Result::BadPointer;
        }

        if (_IFace::IId == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        return Result::NoInterface;
    }
};