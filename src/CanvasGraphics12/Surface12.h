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
    ID3D12Resource *GetD3DResource() { return m_pResource; }
};

