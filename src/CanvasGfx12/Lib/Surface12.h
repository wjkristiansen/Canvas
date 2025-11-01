//================================================================================================
// Surface12
//================================================================================================

#pragma once

#include "D3D12ResourceUtils.h"
#include "CanvasGfx12.h"

//------------------------------------------------------------------------------------------------
class CSurface12 :
    public TGfxElement<Canvas::XGfxSurface>,
    public CResource
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxSurface)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CSurface12(Canvas::XCanvas* pCanvas, ID3D12Resource *pResource, D3D12_RESOURCE_STATES InitState, PCSTR name = nullptr) :
        TGfxElement(pCanvas),
        CResource(pResource, InitState) 
    {
        if (name != nullptr)
            SetName(name);
    }

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    void Rename(ID3D12Resource *pResource) { m_pD3DResource = pResource; }
};
