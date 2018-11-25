// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;

template<class _T>
class CRefCounted : public _T
{
    ULONG m_RefCount = 0;

public:
    virtual ULONG WINAPI AddRef()
    {
        return InterlockedIncrement(&m_RefCount);
    }

    virtual ULONG WINAPI Release()
    {
        ULONG Result = InterlockedDecrement(&m_RefCount);
        if (0 == Result)
        {
            delete(this);
        }
        return Result;
    }
};

class CCanvas
    : public ICanvas
    , public CComObjectRoot
{
    BEGIN_COM_MAP(CCanvas)
        COM_INTERFACE_ENTRY(ICanvas)
    END_COM_MAP()

public:
    CCanvas() = default;
    virtual HRESULT STDMETHODCALLTYPE CreateScene(REFIID riid, void **ppScene)
    {
        return E_NOTIMPL;
    }
};

HRESULT CANVASAPI CreateCanvas(REFIID riid, void **ppCanvas)
{
    *ppCanvas = nullptr;

    try
    {
        if (riid == __uuidof(ICanvas))
        {
            CCanvas *pCanvas = new CComObjectNoLock<CCanvas>(); // throw(bad_alloc)
            *ppCanvas = pCanvas;
            pCanvas->AddRef();
        }
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}
