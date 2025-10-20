//================================================================================================
// Buffer12
//================================================================================================

#pragma once

#include "D3D12ResourceUtils.h"

//------------------------------------------------------------------------------------------------
class CBuffer12 :
    public Gem::TGeneric<Canvas::XGfxBuffer>,
    public CResource
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxBuffer)
    END_GEM_INTERFACE_MAP()

    CBuffer12(ID3D12Resource *pResource, D3D12_RESOURCE_STATES InitState)
        : CResource(pResource, InitState) {}

    Gem::Result Initialize() { return Gem::Result::Success; }    
    void Uninitialize() {}
};
