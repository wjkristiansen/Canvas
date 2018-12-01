// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
class CCanvas
    : public ICanvas
{
public:
    CCanvas() = default;
    CANVASMETHOD(CreateScene)(InterfaceId iid, void **ppScene)
    {
        *ppScene = nullptr;
        try
        {
            CScene *pScene = new CGeneric<CScene>(); // throw(std::bad_alloc)
//            pScene->FinalConstruct();
            *ppScene = pScene;
            pScene->AddRef();
        }
        catch (std::bad_alloc&)
        {
            return Result::OutOfMemory;
        }
        return Result::Success;
    }

    CANVASMETHOD(CreateNode)(PCSTR pName, NODE_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppSceneGraphNode)
    {
        *ppSceneGraphNode = nullptr;
        try
        {
            CComPtr<CSceneGraphNode> pSceneGraphNode = new CSceneGraphNode(flags); // throw(std::bad_alloc)
            *ppSceneGraphNode = pSceneGraphNode;
            return pSceneGraphNode->QueryInterface(iid, ppSceneGraphNode);
        }
        catch (std::bad_alloc&)
        {
            return Result::OutOfMemory;
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
