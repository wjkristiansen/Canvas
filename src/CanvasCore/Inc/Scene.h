//================================================================================================
// Scene
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CScene :
    public CSceneGraphNode<XScene>
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

        return CSceneGraphNode::InternalQueryInterface(iid, ppObj);
    }
};
