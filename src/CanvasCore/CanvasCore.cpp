// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
class CCanvas :
    public ICanvas,
    public CCanvasObjectBase
{
public:
    CCanvas() = default;

    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        *ppObj = nullptr;
        switch (iid)
        {
        case InterfaceId::ICanvas:
            *ppObj = this;
            AddRef();
            break;

        default:
            return CCanvasObjectBase::QueryInterface(iid, ppObj);
        }

        return Result::Success;
    }

    CANVASMETHOD(CreateScene)(InterfaceId iid, void **ppScene)
    {
        *ppScene = nullptr;
        try
        {
            // Create a root scene graph node
            CComPtr<CObject> pRootSceneGraphNode = new CGeneric<CObject>(OBJECT_ELEMENT_FLAG_NONE); // throw(bad_alloc)
            CComPtr<CScene> pScene = new CGeneric<CScene>(pRootSceneGraphNode); // throw(std::bad_alloc)
            return pScene->QueryInterface(iid, ppScene);
        }
        catch (std::bad_alloc&)
        {
            return Result::OutOfMemory;
        }
        return Result::Success;
    }

    CANVASMETHOD(CreateNode)(PCSTR pName, OBJECT_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppSceneGraphNode)
    {
        *ppSceneGraphNode = nullptr;
        try
        {
            return CObject::Create(flags, iid, ppSceneGraphNode);
        }
        catch (std::bad_alloc&)
        {
            return Result::OutOfMemory;
        }
        catch (CanvasError &e)
        {
            return e.Result();
        }
    }
};

//------------------------------------------------------------------------------------------------
Canvas::Result CANVASAPI CreateCanvas(InterfaceId iid, void **ppCanvas)
{
    *ppCanvas = nullptr;

    try
    {
        if (iid == ICanvas::IId)
        {
            CCanvas *pCanvas = new CGeneric<CCanvas>; // throw(bad_alloc)
            *ppCanvas = pCanvas;
            pCanvas->AddRef();
        }
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }

    return Result::Success;
}
