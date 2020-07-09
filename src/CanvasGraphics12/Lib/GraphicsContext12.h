//================================================================================================
// Context
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CCommandAllocatorPool
{
    struct AllocatorElement
    {
        CComPtr<ID3D12CommandAllocator> pCommandAllocator;
        UINT64 FenceValue;
    };

    std::vector<AllocatorElement> CommandAllocators;
    UINT AllocatorIndex = 0;

public:
    CCommandAllocatorPool();

    ID3D12CommandAllocator *Init(CDevice *pDevice, D3D12_COMMAND_LIST_TYPE Type, UINT NumAllocators);
    ID3D12CommandAllocator *RotateAllocators(class CGraphicsContext *pContext);
};

//------------------------------------------------------------------------------------------------
class CGraphicsContext :
    public Canvas::XCanvasGfxGraphicsContext,
    public Gem::CGenericBase
{
    std::mutex m_mutex;

public:
    CComPtr<ID3D12CommandQueue> m_pCommandQueue;
    CComPtr<ID3D12GraphicsCommandList> m_pCommandList;
    CComPtr<ID3D12CommandAllocator> m_pCommandAllocator;
    CCommandAllocatorPool m_CommandAllocatorPool;
    CComPtr<ID3D12DescriptorHeap> m_pShaderResourceDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pSamplerDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pRTVDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pDSVDescriptorHeap;
    CComPtr<ID3D12RootSignature> m_pDefaultRootSig;
    UINT64 m_FenceValue = 0;
    CComPtr<ID3D12Fence> m_pFence;
    CDevice *m_pDevice = nullptr; // weak pointer

    static const UINT NumShaderResourceDescriptors = 65536;
    static const UINT NumSamplerDescriptors = 1024;
    static const UINT NumRTVDescriptors = 64;
    static const UINT NumDSVDescriptors = 64;

    UINT m_NextRTVSlot = 0;

    BEGIN_GEM_INTERFACE_MAP(CGenericBase)
        GEM_INTERFACE_ENTRY(XCanvasGfxGraphicsContext)
    END_GEM_INTERFACE_MAP()

    CGraphicsContext(CDevice *pDevice);
    ~CGraphicsContext();

    // XCanvasGfxGraphicsContext methods
    GEMMETHOD(CreateSwapChain)(HWND hWnd, bool Windowed, XCanvasGfxSwapChain **ppSwapChain, GfxFormat Format, UINT NumBuffers) final;
    GEMMETHOD_(void, CopyBuffer)(XCanvasGfxBuffer *pDest, XCanvasGfxBuffer *pSource) final;
    GEMMETHOD_(void, ClearSurface)(XCanvasGfxSurface *pSurface, const float Color[4]) final;
    GEMMETHOD(Flush)() final;
    GEMMETHOD(FlushAndPresent)(XCanvasGfxSwapChain *pSwapChain) final;
    GEMMETHOD(Wait)() final;

    // Internal functions
    CDevice *GetDevice() const { return m_pDevice; }
    ID3D12CommandQueue *GetD3DCommandQueue() { return m_pCommandQueue; }
    Gem::Result FlushImpl();

    D3D12_CPU_DESCRIPTOR_HANDLE CreateRenderTargetView(class CSurface *pSurface, UINT ArraySlice, UINT MipSlice, UINT PlaneSlice);

    void ApplyResourceBarriers();
};

    