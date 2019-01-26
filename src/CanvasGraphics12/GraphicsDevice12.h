//================================================================================================
// GraphicsDevice12
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CGraphicsDevice12 :
    public CGraphicsDevice
{
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


    CGraphicsDevice12();

    virtual Result Initialize(HWND hWnd, bool Windowed) final;
    virtual Result RenderFrame() final;
    GEMMETHOD(CreateMesh)(const MESH_DATA *pMeshData, XMesh **ppMesh) final;
    GEMMETHOD(CreateCamera)(const CAMERA_DATA *pCameraData, XCamera **ppCamera) final;
    GEMMETHOD(CreateMaterial)(const MATERIAL_DATA *pMaterialData, XMaterial **ppMaterial);
    GEMMETHOD(CreateLight)(const LIGHT_DATA *pLightData, XLight **ppLight);
    GEMMETHOD(AllocateUploadBuffer)(UINT64 SizeInBytes, CUploadBuffer **ppUploadBuffer) final;
};

//------------------------------------------------------------------------------------------------
class CUploadBuffer12 : public CUploadBuffer
{
    CComPtr<ID3D12Resource> m_pResource;
    UINT64 m_OffsetToStart = 0;
    void *m_pData = 0;

public:
    CUploadBuffer12(ID3D12Resource *pResource, UINT64 OffsetToStart, UINT64 Size);
    GEMMETHOD_(void *, Data)() final;
};

//------------------------------------------------------------------------------------------------
class CMaterial12 :
    public Canvas::CMaterial
{

};