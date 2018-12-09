// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CCanvas::InternalQueryInterface(InterfaceId iid, _Outptr_ void **ppObj)
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

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CCanvas::CreateScene(InterfaceId iid, void **ppScene)
{
    *ppScene = nullptr;
    try
    {
        // Create a root scene graph node object
        CCanvasPtr<CSceneGraphNode> pSceneGraphNode;
        InterfaceId iids[] =
        {
            InterfaceId::XSceneGraphNode,
        };
        CreateCustomObject(iids, 1, reinterpret_cast<void **>(&pSceneGraphNode));
        CCanvasPtr<XScene> pScene = new CGeneric<CScene>(pSceneGraphNode); // throw(std::bad_alloc)
        return pScene->QueryInterface(iid, ppScene);
    }
    catch (std::bad_alloc&)
    {
        return Result::OutOfMemory;
    }
    return Result::Success;
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CCanvas::CreateTransformObject(InterfaceId iid, _Outptr_ void **ppObj)
{
    class CTransformObject :
        public XGeneric,
        public CCanvasObjectBase
    {
    public:
        CInnerGeneric<CTransform> m_Transform;
        CInnerGeneric<CSceneGraphNode> m_SceneGraphNode;
        CInnerGeneric<CObjectName> m_ObjectName;

        CTransformObject(CCanvas *pCanvas) :
            CCanvasObjectBase(pCanvas),
            m_Transform(this),
            m_SceneGraphNode(this),
            m_ObjectName(this)
        {}
        virtual ~CTransformObject() {}

        CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
        {
            if (InterfaceId::XTransform == iid)
            {
                return m_Transform.InternalQueryInterface(iid, ppObj);
            }
            if (InterfaceId::XSceneGraphNode == iid)
            {
                return m_SceneGraphNode.InternalQueryInterface(iid, ppObj);
            }

            return __super::InternalQueryInterface(iid, ppObj);
        }
    };

    try
    {
        CCanvasPtr<CTransformObject> pObj = new CGeneric<CTransformObject>(this); // throw(std::bad_alloc)
        return pObj->QueryInterface(iid, ppObj);
    }
    catch(std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
    return Result::NotImplemented;
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CCanvas::CreateCameraObject(_In_z_ InterfaceId iid, _Outptr_ void **ppObj)
{
    return Result::NotImplemented;
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CCanvas::CreateLightObject(_In_z_ InterfaceId iid, _Outptr_ void **ppObj)
{
    return Result::NotImplemented;
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CCanvas::CreateModelInstanceObject(_In_z_ InterfaceId iid, _Outptr_ void **ppObj)
{
    return Result::NotImplemented;
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CCanvas::CreateCustomObject(_In_z_ InterfaceId *pInnerInterfaces, UINT numInnerInterfaces, _Outptr_ void **ppObj)
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
        CCanvasPtr<CCustomObject> pObject = new CGeneric<CCustomObject>(this); // throw(std::bad_alloc)

        for (UINT i = 0; i < numInnerInterfaces; ++i)
        {
            switch (pInnerInterfaces[i])
            {
            case InterfaceId::XSceneGraphNode:
                pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CSceneGraphNode>>(pObject)); // throw(std::bad_alloc)
                break;
            case InterfaceId::XTransform:
                pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CTransform>>(pObject)); // throw(std::bad+alloc)
                break;
            case InterfaceId::XModelInstance:
                pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CModelInstance>>(pObject)); // throw(std::bad+alloc)
                break;
            case InterfaceId::XCamera:
                pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CCamera>>(pObject)); // throw(std::bad+alloc)
                break;
            case InterfaceId::XLight:
                pObject->m_InnerElements.emplace_back(std::make_unique<CInnerGeneric<CLight>>(pObject)); // throw(std::bad+alloc)
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

//------------------------------------------------------------------------------------------------
Result CANVASAPI CreateCanvas(InterfaceId iid, void **ppCanvas)
{
    *ppCanvas = nullptr;

    try
    {
        if (iid == XCanvas::IId)
        {
            CCanvasPtr<CCanvas> pCanvas = new CGeneric<CCanvas>; // throw(bad_alloc)
            return pCanvas->QueryInterface(iid, ppCanvas);
        }
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }

    return Result::Success;
}
