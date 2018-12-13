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
        CreateObject(ObjectType::SceneGraphNode, CANVAS_PPV_ARGS(&pSceneGraphNode), "SceneRoot");
        CCanvasPtr<XScene> pScene = new CGeneric<CScene>(pSceneGraphNode); // throw(std::bad_alloc)
        return pScene->QueryInterface(iid, ppScene);
    }
    catch (std::bad_alloc&)
    {
        return Result::OutOfMemory;
    }
    return Result::Success;
}

template <>
class CCanvasObject<ObjectType::Null> :
    public CCanvasObjectBase
{
public:
    CInnerGeneric<CObjectName> m_ObjectName;
    CCanvasObject(CCanvas *pCanvas, PCSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_ObjectName(this, szName)
    {}

    CANVASMETHOD_(ObjectType, GetType)() const { return ObjectType::Null; }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XObjectName == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

template <>
class CCanvasObject<ObjectType::SceneGraphNode> :
    public CCanvasObjectBase
{
public:
    CInnerGeneric<CObjectName> m_ObjectName;
    CInnerGeneric<CSceneGraphNode> m_SceneGraphNode;

    CCanvasObject(CCanvas *pCanvas, PCSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_SceneGraphNode(this),
        m_ObjectName(this, szName)
    {}

    CANVASMETHOD_(ObjectType, GetType)() const { return ObjectType::SceneGraphNode; }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XObjectName == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }

        if (InterfaceId::XObjectName == iid)
        {
            return m_SceneGraphNode.InternalQueryInterface(iid, ppObj);
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

template <>
class CCanvasObject<ObjectType::Transform> :
    public CCanvasObjectBase
{
public:
    CInnerGeneric<CTransform> m_Transform;
    CInnerGeneric<CSceneGraphNode> m_SceneGraphNode;
    CInnerGeneric<CObjectName> m_ObjectName;

    CCanvasObject(CCanvas *pCanvas, PCSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_Transform(this),
        m_SceneGraphNode(this),
        m_ObjectName(this, szName)
    {}

    CANVASMETHOD_(ObjectType, GetType)() const { return ObjectType::Transform; }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XObjectName == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }
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



//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CCanvas::CreateObject(ObjectType type, InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName)
{
    try
    {
        CCanvasPtr<CCanvasObjectBase> pObj;
        switch (type)
        {
        case ObjectType::Null:
            pObj = new CGeneric<CCanvasObject<ObjectType::Null>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::Transform:
            pObj = new CGeneric<CCanvasObject<ObjectType::Transform>>(this, szName); // throw(std::bad_alloc)
            break;
        }
        return pObj->QueryInterface(iid, ppObj);
    }
    catch(std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
    return Result::NotImplemented;
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
