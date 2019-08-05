//================================================================================================
// Scene
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CScene :
    public TSceneGraphNode<XScene>
{
public:
    CScene(CCanvas *pCanvas, _In_z_ PCWSTR szName);

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XScene::IId == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        return TSceneGraphNode::InternalQueryInterface(iid, ppObj);
    }
};
