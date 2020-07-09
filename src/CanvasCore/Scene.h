//================================================================================================
// Scene
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CScene :
    public TSceneGraphNode<XScene>
{
public:
    BEGIN_GEM_INTERFACE_MAP(TSceneGraphNode<XScene>)
        GEM_INTERFACE_ENTRY(XScene)
    END_GEM_INTERFACE_MAP()

    CScene(CCanvas *pCanvas, _In_z_ PCSTR szName);
};
