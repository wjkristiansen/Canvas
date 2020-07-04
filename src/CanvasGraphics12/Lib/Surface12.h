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
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj)
    {
        if (XCanvasGfxSurface::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return CGenericBase::InternalQueryInterface(iid, ppObj);
    }

    CSurface(ID3D12Resource *pResource, D3D12_RESOURCE_STATES InitState) :
        CResource(pResource, InitState) {}

    void Rename(ID3D12Resource *pResource) { m_pD3DResource = pResource; }
};

