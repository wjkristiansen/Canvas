//================================================================================================
// Scene
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CScene :
    public TSceneGraphNode<XScene>
{
public:
    CScene(CCanvas *pCanvas, _In_z_ PCSTR szName);

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_maybenull_ void **ppObj)
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
