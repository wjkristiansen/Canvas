//================================================================================================
// Light
//================================================================================================

#pragma once

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CLight :
    public TSceneGraphElement<XLight>
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XLight)
    END_GEM_INTERFACE_MAP()

    CLight(CCanvas *pCanvas) :
        TSceneGraphElement(pCanvas) {}
};

}