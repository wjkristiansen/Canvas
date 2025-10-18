//================================================================================================
// Buffer12
//================================================================================================

#pragma once

#include "D3D12ResourceUtils.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CBuffer12 :
    public Gem::TGeneric<XGfxBuffer>,
    public CResource
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxBuffer)
    END_GEM_INTERFACE_MAP()

    CBuffer12(ID3D12Resource *pResource, D3D12_RESOURCE_STATES InitState)
        : CResource(pResource, InitState) {}
};

}
