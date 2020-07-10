//================================================================================================
// Device12
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CDevice12 :
    public Canvas::XGfxDevice,
    public Gem::CGenericBase
{
    CComPtr<ID3D12Resource> m_pVertices;
    CComPtr<ID3D12Resource> m_pNormals;
    CComPtr<ID3D12Resource> m_pTextureUVs[4];
    CComPtr<ID3D12Resource> m_pBoneWeights;
public:
    CComPtr<ID3D12Device5> m_pD3DDevice;
    CResourceStateManager m_ResourceStateManager;

    BEGIN_GEM_INTERFACE_MAP(CGenericBase)
        GEM_INTERFACE_ENTRY(XGfxDevice)
    END_GEM_INTERFACE_MAP()

    CDevice12();

    Result Initialize();

    GEMMETHOD(CreateGraphicsContext)(Canvas::XGfxGraphicsContext **ppGraphicsContext) final;
    // GEMMETHOD(CreateRenderTargetView)(Canvas::XGfxRenderTargetView **ppRTView, Canvas::XGfxTexture2D *pTex2D)
    // GEMMETHOD(AllocateUploadBuffer)(UINT64 SizeInBytes, XGfxUploadBuffer **ppUploadBuffer) final;

    ID3D12Device5 *GetD3DDevice() const { return m_pD3DDevice; }
};

