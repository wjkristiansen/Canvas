// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
class CCanvas :
    public XCanvas,
    public CGenericBase
{
public:
    CCanvas() = default;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        *ppObj = nullptr;
        switch (iid)
        {
        case InterfaceId::XCanvas:
            *ppObj = this;
            AddRef();
            break;

        default:
            return __super::InternalQueryInterface(iid, ppObj);
        }

        return Result::Success;
    }

    CANVASMETHOD(CreateScene)(InterfaceId iid, void **ppScene)
    {
        *ppScene = nullptr;
        try
        {
            // Create a root scene graph node object
            CComPtr<CSceneGraphNode> pSceneGraphNode;
            CObject::Create(OBJECT_ELEMENT_FLAG_SCENE_GRAPH_NODE, InterfaceId::XSceneGraphNode, reinterpret_cast<void **>(&pSceneGraphNode)); // throw(bad_alloc)
            CComPtr<XScene> pScene = new CGeneric<CScene>(pSceneGraphNode); // throw(std::bad_alloc)
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
Result CObject::Create(OBJECT_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppObj)
{
    try
    {
        CComPtr<CObject> pObj = new CGeneric<CObject>(); // throw(std::bad_alloc)

        if (flags & OBJECT_ELEMENT_FLAG_SCENE_GRAPH_NODE)
        {
            pObj->m_Elements.emplace(XSceneGraphNode::IId, std::make_unique<CInnerGeneric<CSceneGraphNode, XSceneGraphNode::IId>>(pObj)); // throw(std::bad_alloc)
        }
        if (flags & OBJECT_ELEMENT_FLAG_TRANSFORM)
        {
            pObj->m_Elements.emplace(XTransform::IId, std::make_unique<CInnerGeneric<CTransform, XTransform::IId>>(pObj)); // throw(std::bad_alloc)
        }
        if (flags & OBJECT_ELEMENT_FLAG_CAMERA)
        {
            pObj->m_Elements.emplace(XCamera::IId, std::make_unique<CInnerGeneric<CCamera, XCamera::IId>>(pObj)); // throw(std::bad_alloc)
        }
        if (flags & OBJECT_ELEMENT_FLAG_LIGHT)
        {
            pObj->m_Elements.emplace(XLight::IId, std::make_unique<CInnerGeneric<CLight, XLight::IId>>(pObj)); // throw(std::bad_alloc)
        }
        if (flags & OBJECT_ELEMENT_FLAG_MODELINSTANCE)
        {
            pObj->m_Elements.emplace(XModelInstance::IId, std::make_unique<CInnerGeneric<CModelInstance, XModelInstance::IId>>(pObj)); // throw(std::bad_alloc)
        }

        return pObj->QueryInterface(iid, ppObj);
    }
    catch(Canvas::Result &res)
    {
        return res;
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CObject::InternalQueryInterface(InterfaceId iid, void **ppUnk)
{
    auto it = m_Elements.find(iid);
    if (it != m_Elements.end())
    {
        // Quick path
        return it->second.get()->InternalQueryInterface(iid, ppUnk);
    }
    else
    {
        // Slow path
        // Iterate through the elements and return the first implementer
        for (it = m_Elements.begin(); it != m_Elements.end(); ++it)
        {
            auto res = it->second->InternalQueryInterface(iid, ppUnk);
            if (res == Result::Success)
            {
                return res;
            }
        }
    }

    // Fall through to base implementation
    return __super::InternalQueryInterface(iid, ppUnk);
}

//------------------------------------------------------------------------------------------------
Canvas::Result CANVASAPI CreateCanvas(InterfaceId iid, void **ppCanvas)
{
    *ppCanvas = nullptr;

    try
    {
        if (iid == XCanvas::IId)
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
