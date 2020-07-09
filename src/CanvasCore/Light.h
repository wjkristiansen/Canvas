//================================================================================================
// Light
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CLight :
    public TSceneGraphNode<XLight>
{
public:
    BEGIN_GEM_INTERFACE_MAP(TSceneGraphNode<XLight>)
        GEM_INTERFACE_ENTRY(XLight)
    END_GEM_INTERFACE_MAP()

    CLight(CCanvas *pCanvas, PCSTR szName) :
        TSceneGraphNode(pCanvas, szName) {}
};
