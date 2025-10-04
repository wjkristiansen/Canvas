//================================================================================================
// Surface12
//================================================================================================

#pragma once

#include "D3D12ResourceUtils.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CSurface12 :
    public Gem::TGeneric<Canvas::XGfxSurface>,
    public CResource
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxSurface)
    END_GEM_INTERFACE_MAP()

    CSurface12(ID3D12Resource *pResource, D3D12_RESOURCE_STATES InitState) :
        CResource(pResource, InitState) {}

    void Rename(ID3D12Resource *pResource) { m_pD3DResource = pResource; }
};

}
