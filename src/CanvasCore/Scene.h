//================================================================================================
// Scene
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CScene :
    public TSceneGraphNode<XScene>
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XScene)
        GEM_INTERFACE_ENTRY(XTransform)
        GEM_INTERFACE_ENTRY(XSceneGraphNode)
        GEM_CONTAINED_INTERFACE_ENTRY(XNameTag, m_NameTag)
    END_GEM_INTERFACE_MAP()

    CScene(CCanvas *pCanvas, _In_z_ PCSTR szName);
};
