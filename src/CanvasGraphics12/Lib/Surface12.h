//================================================================================================
// Surface12
//================================================================================================

#pragma once

#include "D3D12ResourceUtils.h"

//------------------------------------------------------------------------------------------------
class CSurface12 :
    public Canvas::XGfxSurface,
    public CResource,
    public Gem::CGenericBase
{
public:
    BEGIN_GEM_INTERFACE_MAP(CGenericBase)
        GEM_INTERFACE_ENTRY(XGfxSurface)
    END_GEM_INTERFACE_MAP()

    CSurface12(ID3D12Resource *pResource, D3D12_RESOURCE_STATES InitState) :
        CResource(pResource, InitState) {}

    void Rename(ID3D12Resource *pResource) { m_pD3DResource = pResource; }
};

