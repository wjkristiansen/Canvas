// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#include <atlbase.h>

#include <new>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>

// reference additional headers your program requires here
#include <CanvasMath.hpp>
#include <CanvasCore.h>

namespace Canvas
{
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
template<class _T>
class CGeneric : public _T
{
    ULONG m_RefCount = 0;

public:
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
            return _T::QueryInterface(iid, ppObj);
        }

        return Result::Success;
    }
};
}

using namespace Canvas;
#include "Scene.h"
