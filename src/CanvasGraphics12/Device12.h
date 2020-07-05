//================================================================================================
// Device12
//================================================================================================

#pragma once

extern DXGI_FORMAT CanvasFormatToDXGIFormat(Canvas::GfxFormat Fmt);

//------------------------------------------------------------------------------------------------
class CDevice :
    public Canvas::XCanvasGfxDevice,
    public Gem::CGenericBase
{
    QLog::CBasicLogger m_Logger;
    CComPtr<ID3D12Resource> m_pVertices;
    CComPtr<ID3D12Resource> m_pNormals;
    CComPtr<ID3D12Resource> m_pTextureUVs[4];
    CComPtr<ID3D12Resource> m_pBoneWeights;
public:
    CComPtr<ID3D12Device5> m_pD3DDevice;


    CDevice(QLog::CLogClient *pLogClient);

    Result Initialize();

    GEMMETHOD(CreateGfxContext)(HWND hWnd, bool Windowed, Canvas::XCanvasGfxContext **ppGraphicsContext) final;
    // GEMMETHOD(CreateRenderTargetView)(Canvas::XCanvasGfxRenderTargetView **ppRTView, Canvas::XCanvasGfxTexture2D *pTex2D)
    // GEMMETHOD(AllocateUploadBuffer)(UINT64 SizeInBytes, XCanvasGfxUploadBuffer **ppUploadBuffer) final;

    ID3D12Device5 *GetD3DDevice() const { return m_pD3DDevice; }
};

