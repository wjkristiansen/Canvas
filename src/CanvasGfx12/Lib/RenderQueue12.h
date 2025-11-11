//================================================================================================
// Context
//================================================================================================

#pragma once

#include "CanvasGfx12.h"
#include "TaskScheduler.h"

// Enable resource usage validation diagnostics (conflict detection, write exclusivity checking)
// Set to 0 to disable for production builds with minimal overhead
#define CANVAS_RESOURCE_USAGE_DIAGNOSTICS 0

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
// Resource Usage Declaration System
//
// Tasks declare their input/output resource usage to enable:
// 1. Automatic barrier insertion based on resource transitions
// 2. Hazard detection (e.g., concurrent writes to same resource)
// 3. Proper synchronization point insertion
// 4. Elimination of manual barrier management
//
// ResourceUsage types (mutually exclusive for each declared usage):
//   - INPUT_READ: Read access (any sync/access that reads)
//   - OUTPUT_WRITE: Write access (requires exclusive access)
//   - RWM: Read-Write-Modify (atomic operations, etc.)
//   - TRANSITION_ONLY: State transition without actual GPU work
//
// Usage pattern: Task author declares required state at START of GPU work
// RenderQueue automatically generates barriers for state transitions,
// and validates no concurrent writes to same resource from different tasks.
//================================----------------------------------------------------------------

// Usage type enumeration for resource declarations
// Texture usage declaration with layout, access, and sync requirements
// NOTE: Access type determines read/write semantics - no separate UsageType enum
struct TextureUsage
{
    ID3D12Resource* pResource = nullptr;
    
    // Layout state during this task's execution
    D3D12_BARRIER_LAYOUT RequiredLayout = D3D12_BARRIER_LAYOUT_COMMON;
    
    // GPU sync/access needed for this usage - this is the source of truth for what operation is happening
    D3D12_BARRIER_SYNC SyncForUsage = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_ACCESS AccessForUsage = D3D12_BARRIER_ACCESS_NO_ACCESS;
    
    // Granularity control
    UINT Subresources = 0xFFFFFFFF;  // All subresources by default
    D3D12_TEXTURE_BARRIER_FLAGS Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
    
    // Validation helpers - infer from D3D12_BARRIER_ACCESS
    bool IsValid() const { return pResource != nullptr && AccessForUsage != D3D12_BARRIER_ACCESS_NO_ACCESS; }
    bool IsWriteUsage() const
    {
        // Check for any write access bits
        return (AccessForUsage & (D3D12_BARRIER_ACCESS_RENDER_TARGET | 
                                  D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE |
                                  D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
                                  D3D12_BARRIER_ACCESS_COPY_DEST)) != 0;
    }
    bool IsReadUsage() const
    {
        // Check for any read access bits
        return (AccessForUsage & (D3D12_BARRIER_ACCESS_SHADER_RESOURCE |
                                  D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ |
                                  D3D12_BARRIER_ACCESS_COPY_SOURCE |
                                  D3D12_BARRIER_ACCESS_RESOLVE_SOURCE |
                                  D3D12_BARRIER_ACCESS_INDEX_BUFFER |
                                  D3D12_BARRIER_ACCESS_VERTEX_BUFFER |
                                  D3D12_BARRIER_ACCESS_CONSTANT_BUFFER)) != 0;
    }
};

// Buffer usage declaration with access and sync requirements
// NOTE: Access type determines read/write semantics - no separate UsageType enum
struct BufferUsage
{
    ID3D12Resource* pResource = nullptr;
    
    // GPU sync/access needed for this usage - this is the source of truth for what operation is happening
    D3D12_BARRIER_SYNC SyncForUsage = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_ACCESS AccessForUsage = D3D12_BARRIER_ACCESS_NO_ACCESS;
    
    // Granularity control
    UINT64 Offset = 0;
    UINT64 Size = UINT64_MAX;  // Whole resource by default
    
    // Validation helpers - infer from D3D12_BARRIER_ACCESS
    bool IsValid() const { return pResource != nullptr && AccessForUsage != D3D12_BARRIER_ACCESS_NO_ACCESS; }
    bool IsWriteUsage() const
    {
        // Check for any write access bits
        return (AccessForUsage & (D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
                                  D3D12_BARRIER_ACCESS_COPY_DEST)) != 0;
    }
    bool IsReadUsage() const
    {
        // Check for any read access bits
        return (AccessForUsage & (D3D12_BARRIER_ACCESS_SHADER_RESOURCE |
                                  D3D12_BARRIER_ACCESS_COPY_SOURCE |
                                  D3D12_BARRIER_ACCESS_RESOLVE_SOURCE |
                                  D3D12_BARRIER_ACCESS_INDEX_BUFFER |
                                  D3D12_BARRIER_ACCESS_VERTEX_BUFFER |
                                  D3D12_BARRIER_ACCESS_CONSTANT_BUFFER)) != 0;
    }
};

// Container for all resource usages declared by a task
// Enables validation and automatic barrier generation
struct TaskResourceUsages
{
    std::vector<TextureUsage> TextureUsages;
    std::vector<BufferUsage> BufferUsages;
    
    // Convenience methods
    void AddTextureUsage(const TextureUsage& usage) { TextureUsages.push_back(usage); }
    void AddBufferUsage(const BufferUsage& usage) { BufferUsages.push_back(usage); }
    
    bool HasAnyUsage() const { return !TextureUsages.empty() || !BufferUsages.empty(); }
    
    // Validation: check for concurrent writes within this single task's declared resources
    // This checks if the task itself declares conflicting accesses (e.g., writing to same
    // texture with incompatible layouts), which would be an error in task declaration
    bool IsValidNoWriteConflicts() const
    {
        // Check for duplicate write accesses in textures
        for (size_t i = 0; i < TextureUsages.size(); ++i)
        {
            if (!TextureUsages[i].IsWriteUsage())
                continue;
            for (size_t j = i + 1; j < TextureUsages.size(); ++j)
            {
                if (!TextureUsages[j].IsWriteUsage())
                    continue;
                // Two write accesses to same resource = error
                if (TextureUsages[i].pResource == TextureUsages[j].pResource)
                {
                    // Same resource but different subresources might be OK
                    // (though it's usually a mistake). For now, allow different subresources.
                    if (TextureUsages[i].Subresources == TextureUsages[j].Subresources ||
                        TextureUsages[i].Subresources == 0xFFFFFFFF || 
                        TextureUsages[j].Subresources == 0xFFFFFFFF)
                    {
                        return false;  // Conflict!
                    }
                }
            }
        }
        
        // Check for duplicate write accesses in buffers
        for (size_t i = 0; i < BufferUsages.size(); ++i)
        {
            if (!BufferUsages[i].IsWriteUsage())
                continue;
            for (size_t j = i + 1; j < BufferUsages.size(); ++j)
            {
                if (!BufferUsages[j].IsWriteUsage())
                    continue;
                // Two write accesses to same resource = error
                if (BufferUsages[i].pResource == BufferUsages[j].pResource)
                {
                    // Check if ranges overlap
                    UINT64 i_end = BufferUsages[i].Offset + BufferUsages[i].Size;
                    UINT64 j_end = BufferUsages[j].Offset + BufferUsages[j].Size;
                    if (!(i_end <= BufferUsages[j].Offset || j_end <= BufferUsages[i].Offset))
                    {
                        return false;  // Ranges overlap = conflict
                    }
                }
            }
        }
        
        return true;  // No conflicts found
    }
};


// Builder pattern for convenient resource usage declaration
// Use explicit methods that take all parameters - the parameters themselves
// document what the resource is used for (layout, sync, access).
class TaskResourceUsageBuilder
{
public:
    //---------------------------------------------------------------------------------------------
    // Primary texture usage method - explicitly specify all parameters
    //---------------------------------------------------------------------------------------------
    TaskResourceUsageBuilder& SetTextureUsage(
        ID3D12Resource* pResource,
        D3D12_BARRIER_LAYOUT requiredLayout,
        D3D12_BARRIER_SYNC syncForUsage,
        D3D12_BARRIER_ACCESS accessForUsage,
        UINT subresources = 0xFFFFFFFF)
    {
        TextureUsage usage;
        usage.pResource = pResource;
        usage.RequiredLayout = requiredLayout;
        usage.SyncForUsage = syncForUsage;
        usage.AccessForUsage = accessForUsage;
        usage.Subresources = subresources;
        m_usages.AddTextureUsage(usage);
        return *this;
    }
    
    // Convenience: Texture as shader resource (read-only)
    TaskResourceUsageBuilder& TextureAsShaderResource(
        ID3D12Resource* pResource,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pResource,
            D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_PIXEL_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
            subresources);
    }
    
    // Convenience: Texture as unordered access view (read-write)
    TaskResourceUsageBuilder& TextureAsUnorderedAccess(
        ID3D12Resource* pResource,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pResource,
            D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING,
            D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
            subresources);
    }
    
    // Convenience: Texture as render target
    TaskResourceUsageBuilder& TextureAsRenderTarget(
        ID3D12Resource* pResource,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pResource,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET,
            subresources);
    }
    
    // Convenience: Texture as depth-stencil target (write)
    TaskResourceUsageBuilder& TextureAsDepthStencilWrite(
        ID3D12Resource* pResource,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pResource,
            D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
            D3D12_BARRIER_SYNC_DEPTH_STENCIL,
            D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
            subresources);
    }
    
    // Convenience: Texture as depth-stencil resource (read)
    TaskResourceUsageBuilder& TextureAsDepthStencilRead(
        ID3D12Resource* pResource,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pResource,
            D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ,
            D3D12_BARRIER_SYNC_DEPTH_STENCIL,
            D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ,
            subresources);
    }
    
    // Convenience: Texture as copy destination
    TaskResourceUsageBuilder& TextureAsCopyDest(
        ID3D12Resource* pResource,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pResource,
            D3D12_BARRIER_LAYOUT_COMMON,
            D3D12_BARRIER_SYNC_COPY,
            D3D12_BARRIER_ACCESS_COPY_DEST,
            subresources);
    }
    
    // Convenience: Texture as copy source (read-only)
    TaskResourceUsageBuilder& TextureAsCopySource(
        ID3D12Resource* pResource,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pResource,
            D3D12_BARRIER_LAYOUT_COMMON,
            D3D12_BARRIER_SYNC_COPY,
            D3D12_BARRIER_ACCESS_COPY_SOURCE,
            subresources);
    }

    //---------------------------------------------------------------------------------------------
    // Primary buffer usage method - explicitly specify all parameters
    //---------------------------------------------------------------------------------------------
    TaskResourceUsageBuilder& SetBufferUsage(
        ID3D12Resource* pResource,
        D3D12_BARRIER_SYNC syncForUsage,
        D3D12_BARRIER_ACCESS accessForUsage,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        BufferUsage usage;
        usage.pResource = pResource;
        usage.SyncForUsage = syncForUsage;
        usage.AccessForUsage = accessForUsage;
        usage.Offset = offset;
        usage.Size = size;
        m_usages.AddBufferUsage(usage);
        return *this;
    }
    
    // Convenience: Buffer as shader resource (read-only)
    TaskResourceUsageBuilder& BufferAsShaderResource(
        ID3D12Resource* pResource,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pResource,
            D3D12_BARRIER_SYNC_PIXEL_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
            offset,
            size);
    }
    
    // Convenience: Buffer as unordered access (read-write)
    TaskResourceUsageBuilder& BufferAsUnorderedAccess(
        ID3D12Resource* pResource,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pResource,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING,
            D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
            offset,
            size);
    }
    
    // Convenience: Buffer as constant buffer (read-only)
    TaskResourceUsageBuilder& BufferAsConstantBuffer(
        ID3D12Resource* pResource,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pResource,
            D3D12_BARRIER_SYNC_PIXEL_SHADING,
            D3D12_BARRIER_ACCESS_CONSTANT_BUFFER,
            offset,
            size);
    }
    
    // Convenience: Buffer as vertex buffer (read-only)
    TaskResourceUsageBuilder& BufferAsVertexBuffer(
        ID3D12Resource* pResource,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pResource,
            D3D12_BARRIER_SYNC_VERTEX_SHADING,
            D3D12_BARRIER_ACCESS_VERTEX_BUFFER,
            offset,
            size);
    }
    
    // Convenience: Buffer as index buffer (read-only)
    TaskResourceUsageBuilder& BufferAsIndexBuffer(
        ID3D12Resource* pResource,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pResource,
            D3D12_BARRIER_SYNC_INDEX_INPUT,
            D3D12_BARRIER_ACCESS_INDEX_BUFFER,
            offset,
            size);
    }
    
    // Convenience: Buffer as copy destination
    TaskResourceUsageBuilder& BufferAsCopyDest(
        ID3D12Resource* pResource,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pResource,
            D3D12_BARRIER_SYNC_COPY,
            D3D12_BARRIER_ACCESS_COPY_DEST,
            offset,
            size);
    }
    
    // Convenience: Buffer as copy source (read-only)
    TaskResourceUsageBuilder& BufferAsCopySource(
        ID3D12Resource* pResource,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pResource,
            D3D12_BARRIER_SYNC_COPY,
            D3D12_BARRIER_ACCESS_COPY_SOURCE,
            offset,
            size);
    }
    
    const TaskResourceUsages& Build() const { return m_usages; }
    TaskResourceUsages Build() { return m_usages; }

private:
    TaskResourceUsages m_usages;
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
// CRenderQueue12 - D3D12 Command Queue Wrapper
//
// THREADING MODEL: NOT THREAD-SAFE
// - All methods must be called from a single thread
// - Concurrent access from multiple threads will cause undefined behavior
// - For multi-threaded rendering, create multiple RenderQueue instances (one per thread)
// - The mutex member is reserved for potential future use but is currently unused
//------------------------------------------------------------------------------------------------
class CRenderQueue12 :
    public TGfxElement<Canvas::XGfxRenderQueue>
{
    std::mutex m_mutex;  // Reserved for future use; currently unused (single-threaded model)

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
    
    // Frame counter for throttling expensive operations
    uint32_t m_FramesSinceLastRetire = 0;
    
    // Barrier accumulation (batched for efficient recording)
    std::vector<TextureBarrier> m_PendingTextureBarriers;
    std::vector<BufferBarrier> m_PendingBufferBarriers;
    std::vector<GlobalBarrier> m_PendingGlobalBarriers;
    // Pending host-write release tasks that should be scheduled after the next SubmitCommandList
    // Each entry is a pair: (releaseTask, preDependencyTask). preDependencyTask may be NullTaskID.
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
    
    // Task-based GPU workload management with automatic barrier insertion
    
    // Schedule a command list recording task
    Canvas::TaskID ScheduleCommandListRecording(
        std::function<void(ID3D12GraphicsCommandList*)> recordFunc,
        Canvas::TaskID dependsOn = Canvas::NullTaskID);
    
    // Create a GPU fence synchronization point
    Canvas::TaskID CreateGpuSyncPoint(
        UINT64 fenceValue,
        Canvas::TaskID dependsOn = Canvas::NullTaskID);
    
    // Wait for GPU fence on CPU (creates task that blocks on GPU completion)
    Canvas::TaskID WaitForGpuFence(
        UINT64 fenceValue,
        Canvas::TaskID dependsOn = Canvas::NullTaskID);
    
    // Submit command list to GPU (depends on all recording tasks)
    Canvas::TaskID SubmitCommandList(
        Canvas::TaskID dependsOn = Canvas::NullTaskID);
    
    // Schedule swap chain present operation as a task
    Canvas::TaskID SchedulePresent(
        Canvas::XGfxSwapChain* pSwapChain,
        Canvas::TaskID dependsOn = Canvas::NullTaskID);
    
    // Prepare swap chain back buffer for present (transition to PRESENT layout)
    // Automatically depends on the last write to the swap chain back buffer
    // Returns task ID that ensures the transition completes
    Canvas::TaskID PrepareForPresent(
        Canvas::XGfxSwapChain* pSwapChain);

    // Schedule release of a host-write suballocation after the next command list submission completes
    // The release will wait for the GPU fence that corresponds to the next ExecuteCommandLists signal
    void ScheduleHostWriteRelease(const Canvas::GfxSuballocation& suballocation, Canvas::TaskID dependsOn = Canvas::NullTaskID);
    
    //---------------------------------------------------------------------------------------------
    // Resource-Aware Task Scheduling (with automatic barrier insertion)
    //
    // These methods enable tasks to declare their input/output resource usage, eliminating
    // the need for manual barrier management. The RenderQueue automatically:
    // 1. Inserts barriers for layout/access/sync transitions
    // 2. Detects and prevents concurrent writes to the same resource
    // 3. Chains task dependencies based on resource usage conflicts
    //---------------------------------------------------------------------------------------------
    
    // Begin a resource usage scope for batching multiple commands with shared resource declarations
    // All commands recorded between BeginResourceUsage and EndResourceUsage share the same
    // resource state declarations, avoiding redundant declarations for related operations.
    // Automatically inserts barriers once, validates resource conflicts, and chains dependencies.
    //
    // Example:
    //   TaskResourceUsageBuilder usages;
    //   usages.BufferRead(srcBuffer).BufferWrite(dest1).BufferWrite(dest2);
    //   
    //   Canvas::TaskID task = pQueue->BeginResourceUsage(usages.Build(), pDeps, numDeps);
    //   pQueue->ScheduleCommandListRecording([](ID3D12GraphicsCommandList* pCL) { pCL->CopyResource(dest1, src); });
    //   pQueue->ScheduleCommandListRecording([](ID3D12GraphicsCommandList* pCL) { pCL->CopyResource(dest2, src); });
    //   pQueue->EndResourceUsage();  // Returns the same task ID from BeginResourceUsage
    //
    // Parameters:
    //   resourceUsages: Declared resource access patterns (reads, writes, layouts)
    //   pDependencies: Array of task IDs this scope depends on (can be nullptr)
    //   numDependencies: Number of dependencies in pDependencies array
    //
    // Returns: Task ID for the scoped resource usage (same ID returned by EndResourceUsage)
    // Throws: Gem::GemError if resource diagnostics detect write conflicts (when CANVAS_RESOURCE_USAGE_DIAGNOSTICS=1)
    Canvas::TaskID BeginResourceUsage(
        const TaskResourceUsages& resourceUsages,
        const Canvas::TaskID* pDependencies = nullptr,
        size_t numDependencies = 0);
    
    // End a resource usage scope and submit all accumulated commands as a single task
    // Must be called after BeginResourceUsage
    //
    // Returns: Task ID for the recorded scope (same as returned by BeginResourceUsage)
    Canvas::TaskID EndResourceUsage();
    
    // Validate resource usage declarations for write conflicts
    // Returns true if valid (no concurrent writes), false if write conflicts detected
    // Only available when CANVAS_RESOURCE_USAGE_DIAGNOSTICS=1; stub returning true otherwise
    bool ValidateResourceUsageNoWriteConflicts(const TaskResourceUsages& resourceUsages) const;
    
    /// Get resource state snapshot for a specific resource (for debugging/analysis)
    struct ResourceStateSnapshot
    {
        D3D12_BARRIER_LAYOUT CurrentLayout = D3D12_BARRIER_LAYOUT_COMMON;
        Canvas::TaskID LastWriterTask = Canvas::NullTaskID;
        std::vector<Canvas::TaskID> RecentReaders;  // Last N readers for conflict analysis
        bool IsCurrentlyLocked = false;  // Locked for exclusive write access
    };
    
    ResourceStateSnapshot GetResourceState(ID3D12Resource* pResource) const;
    
    // Process completed GPU work and retire tasks
    void ProcessCompletedWork();

private:
    //---------------------------------------------------------------------------------------------
    // Resource usage tracking and barrier generation internals
    //---------------------------------------------------------------------------------------------
    
    // Scoped resource usage state tracking for BeginResourceUsage/RecordCommand/EndResourceUsage API
    struct ResourceUsageScope
    {
        Canvas::TaskID ScopeTaskId = Canvas::NullTaskID;
        TaskResourceUsages ResourceUsages;
        std::vector<std::function<void(ID3D12GraphicsCommandList*)>> AccumulatedCommands;
        bool IsActive = false;
    };
    ResourceUsageScope m_CurrentScope;
    
    // Enhanced resource state tracking for resource-aware tasks
    struct ResourceUsageRecord
    {
        Canvas::TaskID TaskId = Canvas::NullTaskID;
        D3D12_BARRIER_LAYOUT RequiredLayout = D3D12_BARRIER_LAYOUT_COMMON;
        D3D12_BARRIER_SYNC SyncForUsage = D3D12_BARRIER_SYNC_NONE;
        D3D12_BARRIER_ACCESS AccessForUsage = D3D12_BARRIER_ACCESS_NO_ACCESS;
        UINT Subresources = 0xFFFFFFFF;
        UINT64 Offset = 0;
        UINT64 Size = UINT64_MAX;
    };
    
    //---------------------------------------------------------------------------------------------
    // Two-Tier Resource State Tracking System
    //---------------------------------------------------------------------------------------------
    
    // TIER 1: Command Buffer Recording State (Linear, CPU timeline)
    // Tracks layout/sync/access during command buffer recording.
    // State flows forward linearly as commands are recorded.
    struct RecordingResourceState
    {
        D3D12_BARRIER_LAYOUT Layout = D3D12_BARRIER_LAYOUT_COMMON;
        D3D12_BARRIER_SYNC Sync = D3D12_BARRIER_SYNC_NONE;
        D3D12_BARRIER_ACCESS Access = D3D12_BARRIER_ACCESS_NO_ACCESS;
    };
    
    // Current state during command buffer recording (reset when recording completes)
    std::unordered_map<ID3D12Resource*, RecordingResourceState> m_RecordingResourceState;
    
    // TIER 2: Command Submission State (DAG, GPU timeline)
    // Tracks texture layouts after command buffer submissions.
    // Per-task output layouts for barrier generation between submissions.
    struct SubmissionOutputState
    {
        std::unordered_map<ID3D12Resource*, D3D12_BARRIER_LAYOUT> TextureLayouts;
    };
    
    std::unordered_map<Canvas::TaskID, SubmissionOutputState> m_SubmissionOutputLayouts;
    
    // Internal helpers for two-tier system
    
    // Generate barriers for command buffer recording (uses linear recording state)
    void GenerateBarriersForRecording(const TaskResourceUsages& resourceUsages);
    
    // Update recording state after declaring resource usage
    void UpdateRecordingState(const TaskResourceUsages& resourceUsages);
    
    // Merge submission output layouts from multiple dependency tasks
    // Returns combined input state for a new submission task
    SubmissionOutputState MergeSubmissionInputLayouts(
        const Canvas::TaskID* pDependencies,
        size_t numDependencies);
};
