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
