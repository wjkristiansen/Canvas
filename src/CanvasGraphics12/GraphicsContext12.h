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
    static const UINT NumSamplerDescriptors = 1024;
    static const UINT NumRTVDescriptors = 64;
    static const UINT NumDSVDescriptors = 64;

    UINT m_NextRTVSlot = 0;

public:
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj)
    {
        if (XCanvasGfxContext::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return CGenericBase::InternalQueryInterface(iid, ppObj);
    }

    CGraphicsContext(CDevice *pDevice);

    // XCanvasGfxContext methods
    GEMMETHOD(CreateSwapChain)(HWND hWnd, bool Windowed, XCanvasGfxSwapChain **ppSwapChain) final;
    GEMMETHOD_(void, CopyBuffer)(XCanvasGfxBuffer *pDest, XCanvasGfxBuffer *pSource) final;
    GEMMETHOD_(void, ClearSurface)(XCanvasGfxSurface *pSurface, const float Color[4]) final;
    GEMMETHOD(Flush)() final;

    // Helper functions
    D3D12_CPU_DESCRIPTOR_HANDLE CreateRenderTargetView(class CSurface *pSurface, UINT ArraySlice, UINT MipSlice, UINT PlaneSlice);
};

    