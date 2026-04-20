//================================================================================================
// Surface12
//================================================================================================

#pragma once

#include "D3D12ResourceUtils.h"
#include "CanvasGfx12.h"

//------------------------------------------------------------------------------------------------
class CSurface12 :
    public TGfxElement<Canvas::XGfxSurface>,
    public CTextureResource
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxSurface)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CSurface12(Canvas::XCanvas* pCanvas, ID3D12Resource *pResource, D3D12_BARRIER_LAYOUT InitLayout, PCSTR name = nullptr) :
        TGfxElement(pCanvas),
        CTextureResource(pResource, InitLayout),
        m_pOwnerSwapChain(nullptr)
    {
        if (name != nullptr)
        {
            SetName(name);
            SetD3D12DebugName(pResource, name);
        }
    }

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    void Rename(ID3D12Resource *pResource)
    {
        m_pD3DResource = pResource;
        // Propagate the Canvas element name to the new D3D12 resource
        PCSTR name = GetName();
        if (name)
            SetD3D12DebugName(pResource, name);
    }
    
    // Weak pointer to owner swap chain (if this surface is a swap chain back buffer)
    class CSwapChain12* m_pOwnerSwapChain;
};
