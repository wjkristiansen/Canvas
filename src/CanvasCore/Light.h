//================================================================================================
// Light
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CLight :
    public TSceneGraphNode<XLight>
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XLight)
        GEM_INTERFACE_ENTRY(XTransform)
        GEM_INTERFACE_ENTRY(XSceneGraphNode)
        GEM_CONTAINED_INTERFACE_ENTRY(XNameTag, m_NameTag)
    END_GEM_INTERFACE_MAP()

    CLight(CCanvas *pCanvas, PCSTR szName) :
        TSceneGraphNode(pCanvas, szName) {}
};
