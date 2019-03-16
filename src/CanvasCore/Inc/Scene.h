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
    TGemPtr<XSceneGraphNode> m_pRootSceneGraphNode;
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

        if (XName::IId == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }

    GEMMETHOD(GetRootSceneGraphNode)(Gem::InterfaceId iid, _Outptr_ void **ppObj)
    {
        return m_pRootSceneGraphNode->QueryInterface(iid, ppObj);
    }
};
