//================================================================================================
// Transform
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CTransform :
    public XTransform,
    public CInnerGenericBase
{
public:
    CTransform(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}
    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XTransform == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
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

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
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

