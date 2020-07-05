//================================================================================================
// Context
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CGraphicsContext :
    public Canvas::XCanvasGfxContext,
    public Gem::CGenericBase
{
    CComPtr<ID3D12CommandQueue> m_pCommandQueue;
    CComPtr<ID3D12GraphicsCommandList> m_pCommandList;
    CComPtr<ID3D12CommandAllocator> m_pCommandAllocator;
    CComPtr<ID3D12DescriptorHeap> m_pShaderResourceDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pSamplerDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pRTVDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pDSVDescriptorHeap;
    CComPtr<ID3D12RootSignature> m_pDefaultRootSig;
    CDevice *m_pDevice = nullptr; // weak pointer

    static const UINT NumShaderResourceDescriptors = 65536;
    static const UINT NumSamplerDescriptors = 65536;
    static const UINT NumRTVDescriptors = 64;
    static const UINT NumDSVDescriptors = 64;

public:
    CGraphicsContext(HWND hWnd, bool Windowed, ID3D12Device *pDevice);

    // XCanvasGfxContext methods
    GEMMETHOD(CreateSwapChain)(HWND hWnd, bool Windowed, XCanvasGfxSwapChain **ppSwapChain) final;
    GEMMETHOD_(void, CopyBuffer)(XCanvasGfxBuffer *pDest, XCanvasGfxBuffer *pSource) final;
    GEMMETHOD_(void, ClearSurface)(XCanvasGfxSurface *pSurface, const float Color[4]) final;
};

    