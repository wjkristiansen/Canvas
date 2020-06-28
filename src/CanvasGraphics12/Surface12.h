//================================================================================================
// Surface12
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CSurface :
    public Canvas::XCanvasGfxSurface,
    public Gem::CGenericBase
{
    CComPtr<ID3D12Resource> m_pResource;

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

    CSurface(ID3D12Resource *pResource) :
        m_pResource(pResource) {}

    void Rename(ID3D12Resource *pResource) { m_pResource = pResource; }

    ID3D12Resource *GetD3DResource() { return m_pResource; }
};

