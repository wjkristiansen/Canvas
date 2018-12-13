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
template <>
class CCanvasObject<ObjectType::Null> :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    CInnerGeneric<CObjectName> m_ObjectName;
    CCanvasObject(CCanvas *pCanvas, PCSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_ObjectName(this, szName, pCanvas)
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

//------------------------------------------------------------------------------------------------
template <>
class CCanvasObject<ObjectType::SceneGraphNode> :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    CInnerGeneric<CObjectName> m_ObjectName;
    CInnerGeneric<CSceneGraphNode> m_SceneGraphNode;

    CCanvasObject(CCanvas *pCanvas, PCSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_SceneGraphNode(this),
        m_ObjectName(this, szName, pCanvas)
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

//------------------------------------------------------------------------------------------------
template <>
class CCanvasObject<ObjectType::Transform> :
    public XGeneric,
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
        m_ObjectName(this, szName, pCanvas)
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
template <>
class CCanvasObject<ObjectType::Camera> :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    CInnerGeneric<CCamera> m_Camera;
    CInnerGeneric<CTransform> m_Transform;
    CInnerGeneric<CSceneGraphNode> m_SceneGraphNode;
    CInnerGeneric<CObjectName> m_ObjectName;

    CCanvasObject(CCanvas *pCanvas, PCSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_Camera(this),
        m_Transform(this),
        m_SceneGraphNode(this),
        m_ObjectName(this, szName, pCanvas)
    {}

    CANVASMETHOD_(ObjectType, GetType)() const { return ObjectType::Camera; }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XCamera == iid)
        {
            return m_Camera.InternalQueryInterface(iid, ppObj);
        }
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
template <>
class CCanvasObject<ObjectType::Light> :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    CInnerGeneric<CLight> m_Light;
    CInnerGeneric<CTransform> m_Transform;
    CInnerGeneric<CSceneGraphNode> m_SceneGraphNode;
    CInnerGeneric<CObjectName> m_ObjectName;

    CCanvasObject(CCanvas *pCanvas, PCSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_Light(this),
        m_Transform(this),
        m_SceneGraphNode(this),
        m_ObjectName(this, szName, pCanvas)
    {}

    CANVASMETHOD_(ObjectType, GetType)() const { return ObjectType::Light; }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XLight == iid)
        {
            return m_Light.InternalQueryInterface(iid, ppObj);
        }
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
template <>
class CCanvasObject<ObjectType::ModelInstance> :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    CInnerGeneric<CModelInstance> m_ModelInstance;
    CInnerGeneric<CTransform> m_Transform;
    CInnerGeneric<CSceneGraphNode> m_SceneGraphNode;
    CInnerGeneric<CObjectName> m_ObjectName;

    CCanvasObject(CCanvas *pCanvas, PCSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_ModelInstance(this),
        m_Transform(this),
        m_SceneGraphNode(this),
        m_ObjectName(this, szName, pCanvas)
    {}

    CANVASMETHOD_(ObjectType, GetType)() const { return ObjectType::ModelInstance; }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XModelInstance == iid)
        {
            return m_ModelInstance.InternalQueryInterface(iid, ppObj);
        }
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
        CCanvasPtr<XGeneric> pObj;
        switch (type)
        {
        case ObjectType::Scene:
            pObj = new CGeneric<CScene>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::Null:
            pObj = new CGeneric<CCanvasObject<ObjectType::Null>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::SceneGraphNode:
            pObj = new CGeneric<CCanvasObject<ObjectType::SceneGraphNode>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::Transform:
            pObj = new CGeneric<CCanvasObject<ObjectType::Transform>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::Light:
            pObj = new CGeneric<CCanvasObject<ObjectType::Light>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::Camera:
            pObj = new CGeneric<CCanvasObject<ObjectType::Camera>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::ModelInstance:
            pObj = new CGeneric<CCanvasObject<ObjectType::ModelInstance>>(this, szName); // throw(std::bad_alloc)
            break;
        default:
            return Result::NoInterface;
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
