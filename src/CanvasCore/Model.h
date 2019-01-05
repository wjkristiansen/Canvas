//================================================================================================
// Light
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CMeshInstance :
    public XMeshInstance,
    public CInnerGenericBase
{
public:
    CMeshInstance(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (XMeshInstance::IId == iid)
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
class TCanvasObject<ObjectType::MeshInstance> :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    TInnerGeneric<CMeshInstance> m_MeshInstance;
    TInnerGeneric<CTransform> m_Transform;
    TInnerGeneric<CSceneGraphNode> m_SceneGraphNode;
    TInnerGeneric<CObjectName> m_ObjectName;

    TCanvasObject(CCanvas *pCanvas, PCWSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_MeshInstance(this),
        m_Transform(this),
        m_SceneGraphNode(this),
        m_ObjectName(this, szName, pCanvas)
    {}

    GEMMETHOD_(ObjectType, GetType)() const { return ObjectType::MeshInstance; }

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XMeshInstance::IId == iid)
        {
            return m_MeshInstance.InternalQueryInterface(iid, ppObj);
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
