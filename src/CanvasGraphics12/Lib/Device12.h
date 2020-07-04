//================================================================================================
// Device12
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CDevice :
    public Canvas::XCanvasGfxDevice,
    public Gem::CGenericBase
{
    CComPtr<ID3D12Resource> m_pVertices;
    CComPtr<ID3D12Resource> m_pNormals;
    CComPtr<ID3D12Resource> m_pTextureUVs[4];
    CComPtr<ID3D12Resource> m_pBoneWeights;
public:
    CComPtr<ID3D12Device5> m_pD3DDevice;
    CResourceStateManager m_ResourceStateManager;

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj)
    {
        if (XCanvasGfxDevice::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return CGenericBase::InternalQueryInterface(iid, ppObj);
    }

    CDevice();

    Result Initialize();

    GEMMETHOD(CreateGfxContext)(Canvas::XCanvasGfxGraphicsContext **ppGraphicsContext) final;
    // GEMMETHOD(CreateRenderTargetView)(Canvas::XCanvasGfxRenderTargetView **ppRTView, Canvas::XCanvasGfxTexture2D *pTex2D)
    // GEMMETHOD(AllocateUploadBuffer)(UINT64 SizeInBytes, XCanvasGfxUploadBuffer **ppUploadBuffer) final;

    ID3D12Device5 *GetD3DDevice() const { return m_pD3DDevice; }
};

