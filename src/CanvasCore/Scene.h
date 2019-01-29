//================================================================================================
// Scene
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CScene :
    public XScene,
    public CGenericBase,
    public CCanvasListNode
{
public:
    TInnerGeneric<CSceneGraphNode> m_RootSceneGraphNode;
    TInnerGeneric<CObjectName> m_ObjectName;

    CScene(CCanvas *pCanvas, _In_z_ PCWSTR szName);

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XScene::IId == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        if (XObjectName::IId == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }

        if (XSceneGraphNode::IId == iid)
        {
            return m_RootSceneGraphNode.InternalQueryInterface(iid, ppObj);
        }
        return __super::InternalQueryInterface(iid, ppObj);
    }
};
