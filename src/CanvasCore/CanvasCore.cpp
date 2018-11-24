// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;

template<class _T>
class CRefCounted : public _T
{
public:
    UINT RefCount = 0;

    virtual void AddRef()
    {
        InterlockedIncrement(&RefCount);
    }

    virtual UINT Release()
    {
        if (0 == InterlockedDecrement(&RefCount))
        {
            delete(this);
        }
        return RefCount;
    }
};

class CCanvas : 
    public CRefCounted<ICanvas>
{
public:
    CCanvas() = default;
    virtual CanvasResult CreateScene()
    {
        return Canvas::CanvasResult::NotImplemented;
    }
    virtual CanvasResult QueryInterface(UINT iid, void **ppVoid)
    {
        switch (iid)
        {
        case ICanvas::IID:
            *ppVoid = reinterpret_cast<IGeneric *>(this);
            AddRef();
            break;

        case IGeneric::IID:
            *ppVoid = this;
            AddRef();
            break;

        default:
            return CanvasResult::NoInterface;
        }
        return CanvasResult::Success;
    }
};

CanvasResult CANVASAPI CreateCanvas(unsigned int iid, void **ppCanvas)
{
    CCanvas *pCanvas = new CCanvas();

    return CanvasResult::Success;
}
