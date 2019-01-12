//================================================================================================
// GraphicsDevice12
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CGraphicsDevice12 :
    public CGraphicsDevice
{
public:
    CComPtr<ID3D12Device5> m_pD3DDevice;
    CComPtr<ID3D12CommandQueue> m_pDirectCommandQueue;
    CComPtr<IDXGIFactory7> m_pDXGIFactory;
    CComPtr<IDXGISwapChain4> m_pSwapChain;
    CComPtr<ID3D12RootSignature> m_pDefaultRootSig;


    CGraphicsDevice12();

    virtual Result Initialize(HWND hWnd, bool Windowed) final;
    virtual Result RenderFrame() final;
    virtual Result CreateMesh(const MESH_DATA *pMeshData, XMesh **ppMesh) final;
    virtual Result CreateMaterial(const MATERIAL_DATA *pMaterialData) final;
};

class CMaterial12 :
    public Canvas::CMaterial
{

};