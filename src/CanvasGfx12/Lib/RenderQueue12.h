//================================================================================================
// Context
//================================================================================================

#pragma once

#include "CanvasGfx12.h"
#include "TaskScheduler.h"

//------------------------------------------------------------------------------------------------
// Enhanced barrier types for D3D12
struct TextureBarrier
{
    ID3D12Resource* pResource;
    D3D12_BARRIER_SYNC SyncBefore;
    D3D12_BARRIER_SYNC SyncAfter;
    D3D12_BARRIER_ACCESS AccessBefore;
    D3D12_BARRIER_ACCESS AccessAfter;
    D3D12_BARRIER_LAYOUT LayoutBefore;
    D3D12_BARRIER_LAYOUT LayoutAfter;
    UINT Subresources = 0xFFFFFFFF;  // All subresources by default
    D3D12_TEXTURE_BARRIER_FLAGS Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
};

struct BufferBarrier
{
    ID3D12Resource* pResource;
    D3D12_BARRIER_SYNC SyncBefore;
    D3D12_BARRIER_SYNC SyncAfter;
    D3D12_BARRIER_ACCESS AccessBefore;
    D3D12_BARRIER_ACCESS AccessAfter;
    UINT64 Offset = 0;
    UINT64 Size = UINT64_MAX;  // Whole resource by default
};

struct GlobalBarrier
{
    D3D12_BARRIER_SYNC SyncBefore;
    D3D12_BARRIER_SYNC SyncAfter;
    D3D12_BARRIER_ACCESS AccessBefore;
    D3D12_BARRIER_ACCESS AccessAfter;
};

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
// GPU synchronization point
struct GpuSyncPoint
{
    UINT64 FenceValue;
    ID3D12Fence* pFence;
};

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
    ID3D12CommandAllocator *RotateAllocators(class CRenderQueue12 *pRenderQueue);
};

//------------------------------------------------------------------------------------------------
class CRenderQueue12 :
    public TGfxElement<Canvas::XGfxRenderQueue>
{
    std::mutex m_mutex;

public:
    CComPtr<ID3D12CommandQueue> m_pCommandQueue;
    CComPtr<ID3D12GraphicsCommandList> m_pCommandList;
    CComPtr<ID3D12GraphicsCommandList7> m_pCommandList7;  // Cached CL7 for Barrier() to avoid QueryInterface per call
    CComPtr<ID3D12CommandAllocator> m_pCommandAllocator;
    CCommandAllocatorPool m_CommandAllocatorPool;
    CComPtr<ID3D12DescriptorHeap> m_pShaderResourceDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pSamplerDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pRTVDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pDSVDescriptorHeap;
    CComPtr<ID3D12RootSignature> m_pDefaultRootSig;
    UINT64 m_FenceValue = 0;
    CComPtr<ID3D12Fence> m_pFence;
    CDevice12 *m_pDevice = nullptr; // weak pointer

    static const UINT NumShaderResourceDescriptors = 65536;
    static const UINT NumSamplerDescriptors = 1024;
    static const UINT NumRTVDescriptors = 64;
    static const UINT NumDSVDescriptors = 64;

    UINT m_NextRTVSlot = 0;

    // Task scheduling for GPU workloads
    Canvas::TaskScheduler m_TaskScheduler;
    std::unordered_map<Canvas::TaskID, GpuSyncPoint> m_GpuSyncPoints;
    
    // Resource state tracking
    struct ResourceState {
        // Persistent state (survives across command list executions)
        D3D12_BARRIER_LAYOUT CurrentLayout;
        
        // Current command list tracking (reset when command list submits)
        D3D12_BARRIER_SYNC LastSyncInCommandList;
        D3D12_BARRIER_ACCESS LastAccessInCommandList;
        Canvas::TaskID LastUsageTask;
        bool UsedInCurrentCommandList;
    };
    std::unordered_map<ID3D12Resource*, ResourceState> m_ResourceStates;
    std::vector<ID3D12Resource*> m_UsedResourcesDuringCL; // Track only resources touched this command list for fast reset
    
    // Frame counter for throttling expensive operations
    uint32_t m_FramesSinceLastRetire = 0;
    
    // Barrier accumulation (batched for efficient recording)
    std::vector<TextureBarrier> m_PendingTextureBarriers;
    std::vector<BufferBarrier> m_PendingBufferBarriers;
    std::vector<GlobalBarrier> m_PendingGlobalBarriers;
    // Pending host-write release tasks that should be scheduled after the next SubmitCommandList
    // Each entry is a pair: (releaseTask, preDependencyTask). preDependencyTask may be InvalidTaskID.
    std::vector<std::pair<Canvas::TaskID, Canvas::TaskID>> m_PendingHostWriteReleaseTasks;
    
    // Flush pending barriers into command list
    void FlushPendingBarriers();

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxRenderQueue)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CRenderQueue12(Canvas::XCanvas* pCanvas, CDevice12 *pDevice, PCSTR name = nullptr);

    // XGeneric methods
    Gem::Result Initialize() { return Gem::Result::Success; }    
    void Uninitialize();

    // XGfxRenderQueue methods
    GEMMETHOD(CreateSwapChain)(HWND hWnd, bool Windowed, Canvas::XGfxSwapChain **ppSwapChain, Canvas::GfxFormat Format, UINT NumBuffers) final;
    GEMMETHOD_(void, CopyBuffer)(Canvas::XGfxBuffer *pDest, Canvas::XGfxBuffer *pSource) final;
    GEMMETHOD_(void, ClearSurface)(Canvas::XGfxSurface *pSurface, const float Color[4]) final;
    GEMMETHOD(Flush)() final;
    GEMMETHOD(FlushAndPresent)(Canvas::XGfxSwapChain *pSwapChain) final;
    GEMMETHOD(Wait)() final;

    // Internal functions
    CDevice12 *GetDevice() const { return m_pDevice; }
    ID3D12CommandQueue *GetD3DCommandQueue() { return m_pCommandQueue; }
    Gem::Result FlushImpl();

    D3D12_CPU_DESCRIPTOR_HANDLE CreateRenderTargetView(class CSurface12 *pSurface, UINT ArraySlice, UINT MipSlice, UINT PlaneSlice);
    
    // Task-based GPU workload management with enhanced barriers
    
    // Schedule a command list recording task
    Canvas::TaskID ScheduleCommandListRecording(
        std::function<void(ID3D12GraphicsCommandList*)> recordFunc,
        Canvas::TaskID dependsOn = Canvas::InvalidTaskID);
    
    // Ensure texture is in the required layout, automatically inserting barrier if needed
    // Tracks both persistent layout AND current command list access to detect hazards
    // syncBefore/After and accessBefore/After describe the barrier boundaries for this operation
    // Returns the task that ensures the layout (may be InvalidTaskID if no barrier needed)
    Canvas::TaskID EnsureTextureLayout(
        ID3D12Resource* pResource,
        D3D12_BARRIER_SYNC syncAfter,
        D3D12_BARRIER_ACCESS accessAfter,
        D3D12_BARRIER_LAYOUT requiredLayout,
        UINT subresources = 0xFFFFFFFF,
        D3D12_TEXTURE_BARRIER_FLAGS flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
        Canvas::TaskID dependsOn = Canvas::InvalidTaskID);
    
    // For buffers, track access within command list to detect hazards
    // Returns barrier task if needed, or last usage task
    Canvas::TaskID EnsureBufferAccess(
        ID3D12Resource* pResource,
        D3D12_BARRIER_SYNC syncAfter,
        D3D12_BARRIER_ACCESS accessAfter,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX,
        Canvas::TaskID dependsOn = Canvas::InvalidTaskID);
    
    // Schedule explicit buffer barrier
    Canvas::TaskID ScheduleBufferBarrier(
        ID3D12Resource* pResource,
        D3D12_BARRIER_SYNC syncBefore,
        D3D12_BARRIER_SYNC syncAfter,
        D3D12_BARRIER_ACCESS accessBefore,
        D3D12_BARRIER_ACCESS accessAfter,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX,
        Canvas::TaskID dependsOn = Canvas::InvalidTaskID);
    
    // Schedule explicit texture barrier (for advanced scenarios)
    Canvas::TaskID ScheduleTextureBarrier(
        const TextureBarrier& barrier,
        Canvas::TaskID dependsOn = Canvas::InvalidTaskID);
    
    Canvas::TaskID ScheduleGlobalBarrier(
        const GlobalBarrier& barrier,
        Canvas::TaskID dependsOn = Canvas::InvalidTaskID);
    
    // Schedule multiple barriers at once (most efficient for explicit barriers)
    Canvas::TaskID ScheduleBarrierGroup(
        const std::vector<TextureBarrier>& textureBarriers,
        const std::vector<BufferBarrier>& bufferBarriers,
        const std::vector<GlobalBarrier>& globalBarriers,
        Canvas::TaskID dependsOn = Canvas::InvalidTaskID);
    
    // Create a GPU fence synchronization point
    Canvas::TaskID CreateGpuSyncPoint(
        UINT64 fenceValue,
        Canvas::TaskID dependsOn = Canvas::InvalidTaskID);
    
    // Wait for GPU fence on CPU (creates task that blocks on GPU completion)
    Canvas::TaskID WaitForGpuFence(
        UINT64 fenceValue,
        Canvas::TaskID dependsOn = Canvas::InvalidTaskID);
    
    // Submit command list to GPU (depends on all recording tasks)
    Canvas::TaskID SubmitCommandList(
        Canvas::TaskID dependsOn = Canvas::InvalidTaskID);

    // Schedule release of a host-write suballocation after the next command list submission completes
    // The release will wait for the GPU fence that corresponds to the next ExecuteCommandLists signal
    void ScheduleHostWriteRelease(const Canvas::GfxSuballocation& suballocation, Canvas::TaskID dependsOn = Canvas::InvalidTaskID);
    
    // Process completed GPU work and retire tasks
    void ProcessCompletedWork();
};
