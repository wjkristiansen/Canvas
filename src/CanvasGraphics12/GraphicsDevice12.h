//================================================================================================
// GraphicsDevice12
//================================================================================================

#pragma once

class CContext :
    public Canvas::XCanvasGSContext,
    public Gem::CGenericBase
{
    CComPtr<ID3D12CommandQueue> m_pCommandQueue;
    TGemPtr<XCanvasGSDevice> m_pXDevice;
    CComPtr<ID3D12Device> m_pDevice12;
};

//------------------------------------------------------------------------------------------------
class CDevice :
    public Canvas::XCanvasGSDevice,
    public Gem::CGenericBase
{
    QLog::CBasicLogger m_Logger;
    CComPtr<ID3D12Resource> m_pVertices;
    CComPtr<ID3D12Resource> m_pNormals;
    CComPtr<ID3D12Resource> m_pTextureUVs[4];
    CComPtr<ID3D12Resource> m_pBoneWeights;
public:
    CComPtr<ID3D12Device5> m_pD3DDevice;
    CComPtr<ID3D12CommandQueue> m_pDirectCommandQueue;
    CComPtr<IDXGIFactory7> m_pDXGIFactory;
    CComPtr<IDXGISwapChain4> m_pSwapChain;
    CComPtr<ID3D12RootSignature> m_pDefaultRootSig;


    CDevice(QLog::CLogClient *pLogClient);

    Result Initialize(HWND hWnd, bool Windowed);

    GEMMETHOD(Present)() final;
    GEMMETHOD(CreateGraphicsContext)(Canvas::XCanvasGSContext **ppGraphicsContext) final;
    // GEMMETHOD(AllocateUploadBuffer)(UINT64 SizeInBytes, XCanvasGSUploadBuffer **ppUploadBuffer) final;
};

//------------------------------------------------------------------------------------------------
class CUploadBuffer : 
    public Canvas::XCanvasGSUploadBuffer,
    public Gem::CGenericBase
{
    CComPtr<ID3D12Resource> m_pResource;
    UINT64 m_OffsetToStart = 0;
    void *m_pData = 0;

public:
    CUploadBuffer(ID3D12Resource *pResource, UINT64 OffsetToStart, UINT64 Size);
    GEMMETHOD_(void *, Data)() final;
};

