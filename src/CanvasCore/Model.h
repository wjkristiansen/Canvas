//================================================================================================
// Light
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CModelInstance :
    public XModelInstance,
    public CInnerGenericBase
{
public:
    CModelInstance(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}
    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XModelInstance == iid)
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

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
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

