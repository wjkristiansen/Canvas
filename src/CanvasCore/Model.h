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
    GOMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (XModelInstance::IId == iid)
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
class TCanvasObject<ObjectType::ModelInstance> :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    TInnerGeneric<CModelInstance> m_ModelInstance;
    TInnerGeneric<CTransform> m_Transform;
    TInnerGeneric<CSceneGraphNode> m_SceneGraphNode;
    TInnerGeneric<CObjectName> m_ObjectName;

    TCanvasObject(CCanvas *pCanvas, PCWSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_ModelInstance(this),
        m_Transform(this),
        m_SceneGraphNode(this),
        m_ObjectName(this, szName, pCanvas)
    {}

    GOMMETHOD_(ObjectType, GetType)() const { return ObjectType::ModelInstance; }

    GOMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XModelInstance::IId == iid)
        {
            return m_ModelInstance.InternalQueryInterface(iid, ppObj);
        }
        if (XObjectName::IId == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }
        if (XTransform::IId == iid)
        {
            return m_Transform.InternalQueryInterface(iid, ppObj);
        }
        if (XSceneGraphNode::IId == iid)
        {
            return m_SceneGraphNode.InternalQueryInterface(iid, ppObj);
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

