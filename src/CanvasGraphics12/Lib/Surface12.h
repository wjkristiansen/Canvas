//================================================================================================
// Surface12
//================================================================================================

#pragma once

#include "D3D12ResourceUtils.h"

//------------------------------------------------------------------------------------------------
class CSurface :
    public Canvas::XCanvasGfxSurface,
    public CResource,
    public Gem::CGenericBase
{
public:
    BEGIN_GEM_INTERFACE_MAP(CGenericBase)
        GEM_INTERFACE_ENTRY(XCanvasGfxSurface)
    END_GEM_INTERFACE_MAP()

    CSurface(ID3D12Resource *pResource, D3D12_RESOURCE_STATES InitState) :
        CResource(pResource, InitState) {}

    void Rename(ID3D12Resource *pResource) { m_pD3DResource = pResource; }
};

