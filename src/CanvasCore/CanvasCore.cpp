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
        //*ppScene = nullptr;
        //try
        //{
        //    // Create a root scene graph node object
        //    CComPtr<CSceneGraphNode> pSceneGraphNode;
        //    CObject::Create(OBJECT_ELEMENT_FLAG_SCENE_GRAPH_NODE, XSceneGraphNode, reinterpret_cast<void **>(&pSceneGraphNode)); // throw(bad_alloc)
        //    CComPtr<XScene> pScene = new CInnerGeneric<CScene>(pSceneGraphNode); // throw(std::bad_alloc)
        //    return pScene->QueryInterface(iid, ppScene);
        //}
        //catch (std::bad_alloc&)
        //{
        //    return Result::OutOfMemory;
        //}
        return Result::Success;
    }
};

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

//------------------------------------------------------------------------------------------------
Result CObjectFactory::CreateObject(InterfaceId *pInnerInterfaces, UINT numInnerInterfaces, _Outptr_ void **ppObj)
{
    Result res = Result::Success;

    if (numInnerInterfaces == 0)
    {
        return Result::InvalidArg;
    }

    if (pInnerInterfaces == nullptr)
    {
        return Result::BadPointer;
    }

    try
    {
        CComPtr<CObject> pObject = new CGeneric<CObject>(); // throw(std::bad_alloc)

        for (UINT i = 0; i < numInnerInterfaces; ++i)
        {
            switch (pInnerInterfaces[i])
            {
            case InterfaceId::XSceneGraphNode:
                pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CSceneGraphNode, InterfaceId::XSceneGraphNode>>(pObject)); // throw(std::bad_alloc)
                break;
            case InterfaceId::XTransform:
                pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CTransform, InterfaceId::XTransform>>(pObject)); // throw(std::bad+alloc)
                break;
            case InterfaceId::XModelInstance:
                pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CModelInstance, InterfaceId::XModelInstance>>(pObject)); // throw(std::bad+alloc)
                break;
            case InterfaceId::XCamera:
                pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CCamera, InterfaceId::XCamera>>(pObject)); // throw(std::bad+alloc)
                break;
            case InterfaceId::XLight:
                pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CLight, InterfaceId::XLight>>(pObject)); // throw(std::bad+alloc)
                break;
            }
        }

        return pObject->QueryInterface(pInnerInterfaces[0], ppObj);
    }
    catch (std::bad_alloc&)
    {
        return Result::OutOfMemory;
    }
}
