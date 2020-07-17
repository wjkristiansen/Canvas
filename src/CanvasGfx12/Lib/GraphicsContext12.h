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

    ID3D12CommandAllocator *Init(CDevice12 *pDevice, D3D12_COMMAND_LIST_TYPE Type, UINT NumAllocators);
    ID3D12CommandAllocator *RotateAllocators(class CGraphicsContext12 *pContext, UINT64 CompletedFenceValue);
};

//------------------------------------------------------------------------------------------------
class CManagedGpuDescriptorHeap
{
public:
    struct AllocationRecord
    {
        AllocationRecord(UINT size, UINT64 fenceValue) :
            FenceValue(fenceValue),
            Size(size) {}
        UINT64 FenceValue = 0;
        UINT Size = 0;
    };

    std::deque<AllocationRecord> m_ActiveAllocations;

    TRingSuballocator<UINT> m_RingSuballocator;
    CComPtr<ID3D12DescriptorHeap> m_pHeap; 
    D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc = {};
    UINT m_DescriptorIncremenent = 0;

    CManagedGpuDescriptorHeap() = default;

    Gem::Result Initialize(ID3D12Device *pDevice, const D3D12_DESCRIPTOR_HEAP_DESC &HeapDesc);

    UINT HeapSize() const { return m_HeapDesc.NumDescriptors; }

    UINT64 GetMinFenceValue() const { return m_ActiveAllocations.empty() ? UINT64(0) : m_ActiveAllocations.begin()->FenceValue; }
    UINT64 GetMaxFenceValue() const { return m_ActiveAllocations.empty() ? UINT64(0) : m_ActiveAllocations.rbegin()->FenceValue; }

    void FreeDescriptors(UINT64 FenceValue);
    UINT AllocateDescriptors(UINT NumDescriptors, UINT64 FenceValue);

    D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptorHandle(UINT Index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE Handle;
        Handle.ptr = m_pHeap->GetCPUDescriptorHandleForHeapStart().ptr + m_DescriptorIncremenent * Index;
        return Handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptorHandle(UINT Index) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE Handle;
        Handle.ptr = m_pHeap->GetGPUDescriptorHandleForHeapStart().ptr + m_DescriptorIncremenent * Index;
        return Handle;
    }
};

//------------------------------------------------------------------------------------------------
class CGraphicsContext12 :
    public Canvas::XGfxGraphicsContext,
    public Gem::CGenericBase
{
    std::mutex m_mutex;

public:
    CComPtr<ID3D12CommandQueue> m_pCommandQueue;
    CComPtr<ID3D12GraphicsCommandList> m_pCommandList;
    CComPtr<ID3D12CommandAllocator> m_pCommandAllocator;
    CCommandAllocatorPool m_CommandAllocatorPool;
    CManagedGpuDescriptorHeap m_ShaderResourceDescriptorHeap;
    CManagedGpuDescriptorHeap m_SamplerDescriptorHeap;
    CManagedGpuDescriptorHeap m_RTVDescriptorHeap;
    CManagedGpuDescriptorHeap m_DSVDescriptorHeap;
    CComPtr<ID3D12RootSignature> m_pDefaultRootSig;
    UINT64 m_FenceValue = 0;
    CComPtr<ID3D12Fence> m_pFence;
    CDevice12 *m_pDevice = nullptr; // weak pointer

    static const UINT NumShaderResourceDescriptors = 65536;
    static const UINT NumSamplerDescriptors = 1024;
    static const UINT NumRTVDescriptors = 64;
    static const UINT NumDSVDescriptors = 64;

    UINT m_NextRTVSlot = 0;

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxGraphicsContext)
    END_GEM_INTERFACE_MAP()

    CGraphicsContext12(CDevice12 *pDevice);
    ~CGraphicsContext12();

    // XGfxGraphicsContext methods
    GEMMETHOD(CreateSwapChain)(HWND hWnd, bool Windowed, XGfxSwapChain **ppSwapChain, GfxFormat Format, UINT NumBuffers) final;
    GEMMETHOD_(void, CopyBuffer)(XGfxBuffer *pDest, XGfxBuffer *pSource) final;
    GEMMETHOD_(void, ClearSurface)(XGfxSurface *pSurface, const float Color[4]) final;
    GEMMETHOD(Flush)() final;
    GEMMETHOD(FlushAndPresent)(XGfxSwapChain *pSwapChain) final;
    GEMMETHOD(Wait)() final;

    // Internal functions
    CDevice12 *GetDevice() const { return m_pDevice; }
    ID3D12CommandQueue *GetD3DCommandQueue() { return m_pCommandQueue; }
    Gem::Result FlushImpl();

    void ApplyResourceBarriers();
};

    