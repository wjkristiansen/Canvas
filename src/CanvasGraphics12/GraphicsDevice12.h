//================================================================================================
// GraphicsDevice12
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CGraphicsDevice12 :
    public XGraphicsDevice,
    public CGenericBase
{
    CComPtr<ID3D12Device5> m_pD3DDevice;

    CGraphicsDevice12();

    virtual Result Initialize();
};