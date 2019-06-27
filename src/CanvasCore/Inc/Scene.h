//================================================================================================
// Scene
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CScene :
    public XScene,
    public CSceneGraphNode
{
public:
    TInnerGeneric<CName> m_ObjectName;

    CScene(CCanvas *pCanvas, _In_z_ PCWSTR szName);

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XScene::IId == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        if (XSceneGraphNode::IId == iid)
        {
            *ppObj = reinterpret_cast<XSceneGraphNode *>(this);
            AddRef();
            return Result::Success;
        }

        if (XName::IId == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }

        return CSceneGraphNode::InternalQueryInterface(iid, ppObj);
    }

    //GEMMETHOD(GetRootSceneGraphNode)(Gem::InterfaceId iid, _Outptr_ void **ppObj)
    //{
    //    return m_pRootSceneGraphNode->QueryInterface(iid, ppObj);
    //}
};
