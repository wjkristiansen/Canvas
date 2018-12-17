//================================================================================================
// Scene
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CScene :
    public XScene,
    public CCanvasObjectBase
{
public:
    TInnerGeneric<CSceneGraphNode> m_RootSceneGraphNode;
    TInnerGeneric<CObjectName> m_ObjectName;

    CScene(CCanvas *pCanvas, _In_z_ PCWSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_RootSceneGraphNode(this),
        m_ObjectName(this, szName, pCanvas)
    {
    }

    CANVASMETHOD_(ObjectType, GetType)() const final { return ObjectType::Scene; }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (InterfaceId::XScene == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        if (InterfaceId::XObjectName == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }

        if (InterfaceId::XSceneGraphNode == iid)
        {
            return m_RootSceneGraphNode.InternalQueryInterface(iid, ppObj);
        }
        return __super::InternalQueryInterface(iid, ppObj);
    }
};
