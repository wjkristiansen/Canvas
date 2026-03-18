//================================================================================================
// Buffer12
//================================================================================================

#pragma once

#include "D3D12ResourceUtils.h"
#include "CanvasGfx12.h"

//------------------------------------------------------------------------------------------------
class CBuffer12 :
    public TGfxElement<Canvas::XGfxBuffer>,
    public CResource
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxBuffer)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CBuffer12(Canvas::XCanvas* pCanvas, ID3D12Resource *pResource, PCSTR name = nullptr)
        : TGfxElement(pCanvas),
          CResource(pResource)
    {
        if (name != nullptr)
            SetName(name);
    }

    Gem::Result Initialize() { return Gem::Result::Success; }    
    void Uninitialize() {}
};
