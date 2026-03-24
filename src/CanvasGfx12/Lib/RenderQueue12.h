//================================================================================================
// Context
//================================================================================================

#pragma once

#include "CanvasGfx12.h"
#include "GpuTask.h"

// Forward declarations
class CSurface12;
class CSwapChain12;

// Enable resource usage validation diagnostics (conflict detection, write exclusivity checking)
// Set to 0 to disable for production builds with minimal overhead
#define CANVAS_RESOURCE_USAGE_DIAGNOSTICS 0

//------------------------------------------------------------------------------------------------
// Resource Usage Declaration System
//
// GPU operations declare their resource usage to enable:
// 1. Automatic barrier insertion based on resource transitions
// 2. Hazard detection (e.g., concurrent writes to same resource)
// 3. Proper synchronization point insertion
// 4. Elimination of manual barrier management
//
// Usage pattern: Caller declares required state at START of GPU work.
// RenderQueue automatically generates barriers for state transitions.
//================================----------------------------------------------------------------

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
struct ResourceUsages
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
class ResourceUsageBuilder
{
public:
    //---------------------------------------------------------------------------------------------
    // Primary texture usage method - explicitly specify all parameters
    //---------------------------------------------------------------------------------------------
    ResourceUsageBuilder& SetTextureUsage(
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
    ResourceUsageBuilder& TextureAsShaderResource(
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
    ResourceUsageBuilder& TextureAsUnorderedAccess(
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
    ResourceUsageBuilder& TextureAsRenderTarget(
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
    ResourceUsageBuilder& TextureAsDepthStencilWrite(
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
    ResourceUsageBuilder& TextureAsDepthStencilRead(
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
    ResourceUsageBuilder& TextureAsCopyDest(
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
    ResourceUsageBuilder& TextureAsCopySource(
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
    ResourceUsageBuilder& SetBufferUsage(
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
    ResourceUsageBuilder& BufferAsShaderResource(
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
    ResourceUsageBuilder& BufferAsUnorderedAccess(
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
    ResourceUsageBuilder& BufferAsConstantBuffer(
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
    ResourceUsageBuilder& BufferAsVertexBuffer(
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
    ResourceUsageBuilder& BufferAsIndexBuffer(
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
    ResourceUsageBuilder& BufferAsCopyDest(
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
    ResourceUsageBuilder& BufferAsCopySource(
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
    
    const ResourceUsages& Build() const { return m_usages; }
    ResourceUsages Build() { return m_usages; }

private:
    ResourceUsages m_usages;
};

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

    // UI overlay command list — records text/HUD draws in parallel with scene rendering.
    // Submitted after the scene command list on the same queue so GPU execution is serialized.
    CComPtr<ID3D12GraphicsCommandList> m_pUICommandList;
    CComPtr<ID3D12GraphicsCommandList7> m_pUICommandList7;
    CComPtr<ID3D12CommandAllocator> m_pUICommandAllocator;
    CCommandAllocatorPool m_UICommandAllocatorPool;
    bool m_UICommandListOpen = false;

    // Fixup command list — bridges actual committed layouts to assumed initial layouts
    // at ExecuteCommandLists time. Prepended before the CL whose assumptions may differ.
    CComPtr<ID3D12GraphicsCommandList> m_pFixupCommandList;
    CComPtr<ID3D12GraphicsCommandList7> m_pFixupCommandList7;
    CComPtr<ID3D12CommandAllocator> m_pFixupCommandAllocator;
    CCommandAllocatorPool m_FixupCommandAllocatorPool;
    CComPtr<ID3D12DescriptorHeap> m_pShaderResourceDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pSamplerDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pRTVDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pDSVDescriptorHeap;
    CComPtr<ID3D12RootSignature> m_pDefaultRootSig;
    CComPtr<ID3D12PipelineState> m_pDefaultPSO;
    CComPtr<ID3D12RootSignature> m_pTextRootSig;
    CComPtr<ID3D12PipelineState> m_pTextPSO;
    DXGI_FORMAT m_TextPSOFormat = DXGI_FORMAT_UNKNOWN;
    UINT64 m_FenceValue = 0;
    CComPtr<ID3D12Fence> m_pFence;
    CDevice12 *m_pDevice = nullptr; // weak pointer

    // Depth buffer for rendering
    Gem::TGemPtr<CSurface12> m_pDepthBuffer;
    UINT m_DepthBufferWidth = 0;
    UINT m_DepthBufferHeight = 0;
    UINT m_NextDSVSlot = 0;

    // G-buffer render targets for deferred shading
    Gem::TGemPtr<CSurface12> m_pGBufferNormals;
    Gem::TGemPtr<CSurface12> m_pGBufferDiffuseColor;
    Canvas::GfxFormat m_GBufferNormalsFormat = Canvas::GfxFormat::R10G10B10A2_UNorm;
    Canvas::GfxFormat m_GBufferDiffuseFormat = Canvas::GfxFormat::R10G10B10A2_UNorm;
    UINT m_GBufferWidth = 0;
    UINT m_GBufferHeight = 0;

    // Composition pass PSO and root signature
    CComPtr<ID3D12PipelineState> m_pCompositePSO;
    CComPtr<ID3D12RootSignature> m_pCompositeRootSig;

    // Frame rendering state
    CSwapChain12 *m_pCurrentSwapChain = nullptr;   // Set during BeginFrame..EndFrame
    Canvas::XCamera *m_pActiveCamera = nullptr;     // Set by scene via SetActiveCamera during Update
    D3D12_CPU_DESCRIPTOR_HANDLE m_CurrentRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_CurrentDSV = {};

    // Renderable elements enqueued during scene graph update, dispatched during EndFrame
    std::vector<Canvas::XSceneGraphElement*> m_RenderableQueue;

    // SRV descriptor allocation for per-draw structured buffers
    UINT m_NextSRVSlot = 0;

    static const UINT NumShaderResourceDescriptors = 65536;
    static const UINT NumSamplerDescriptors = 1024;
    static const UINT NumRTVDescriptors = 64;
    static const UINT NumDSVDescriptors = 64;

    UINT m_NextRTVSlot = 0;

    // GPU sync point tracking (fence-value based)
    std::unordered_map<UINT64, GpuSyncPoint> m_GpuSyncPoints;
    
    // Pending upload allocation retirements: freed once GPU advances past the fence value
    struct PendingUploadRetirement
    {
        Canvas::GfxSuballocation Suballocation;
        UINT64 FenceValue;  // Release once GPU completes past this fence value
    };
    std::vector<PendingUploadRetirement> m_PendingUploadRetirements;

    // Scratch buffer for fixup barrier computation (reused across frames)
    std::vector<D3D12_TEXTURE_BARRIER> m_FixupBarriers;

    // Scratch buffers for barrier emission (reused across calls)
    std::vector<D3D12_TEXTURE_BARRIER> m_EmitTexBarriers;
    std::vector<D3D12_BUFFER_BARRIER> m_EmitBufBarriers;
    std::vector<D3D12_BARRIER_GROUP> m_EmitBarrierGroups;

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxRenderQueue)
        GEM_INTERFACE_ENTRY(Canvas::XRenderQueue)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CRenderQueue12(Canvas::XCanvas* pCanvas, CDevice12 *pDevice, PCSTR name = nullptr);

    // XGeneric methods
    Gem::Result Initialize() { return Gem::Result::Success; }    
    void Uninitialize();

    // XGfxRenderQueue methods
    GEMMETHOD(CreateSwapChain)(HWND hWnd, bool Windowed, Canvas::XGfxSwapChain **ppSwapChain, Canvas::GfxFormat Format, UINT NumBuffers) final;
    GEMMETHOD(FlushAndPresent)(Canvas::XGfxSwapChain *pSwapChain) final;
    GEMMETHOD(BeginFrame)(Canvas::XGfxSwapChain *pSwapChain) final;
    GEMMETHOD(DrawMesh)(Canvas::XGfxMeshData *pMeshData, const Canvas::GfxPerObjectConstants &objectConstants) final;
    GEMMETHOD(DrawText)(const void *pVertexData, uint32_t vertexCount, Canvas::XGfxSurface *pGlyphAtlas, const Canvas::Math::FloatVector4 &screenOffset) final;
    GEMMETHOD(UploadTextureRegion)(Canvas::XGfxSurface *pDstSurface, uint32_t dstX, uint32_t dstY, uint32_t width, uint32_t height, const void *pData, uint32_t srcRowPitch, Canvas::GfxRenderContext context) final;
    GEMMETHOD(SubmitForRender)(Canvas::XSceneGraphElement *pElement) final;
    GEMMETHOD_(void, SetActiveCamera)(Canvas::XCamera *pCamera) final;
    GEMMETHOD(EndFrame)() final;

    // Internal functions
    CDevice12 *GetDevice() const { return m_pDevice; }
    ID3D12CommandQueue *GetD3DCommandQueue() { return m_pCommandQueue; }

    // Flush: compute final layouts, update committed state, close/submit CL
    void Flush();

    D3D12_CPU_DESCRIPTOR_HANDLE CreateRenderTargetView(class CSurface12 *pSurface, UINT ArraySlice, UINT MipSlice, UINT PlaneSlice);
    D3D12_CPU_DESCRIPTOR_HANDLE CreateDepthStencilView(class CSurface12 *pSurface);
    
    // Create or resize the depth buffer to match the given dimensions
    void EnsureDepthBuffer(UINT width, UINT height);

    // Create or resize G-buffer render targets to match the given dimensions
    void EnsureGBuffers(UINT width, UINT height);
    
    // Create the default (geometry pass) PSO (lazily, on first use)
    void EnsureDefaultPSO();

    // Create the composition PSO and root signature (lazily, on first use)
    void EnsureCompositePSO(DXGI_FORMAT rtvFormat);
    
    // Create the text PSO and root signature (lazily, on first use)
    void EnsureTextPSO(DXGI_FORMAT rtvFormat);
    
    // Allocate a shader-visible SRV descriptor slot and return GPU handle
    D3D12_GPU_DESCRIPTOR_HANDLE CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);
    
    // Signal the GPU fence and record a sync point
    void CreateGpuSyncPoint(UINT64 fenceValue);
    
    // Wait on CPU for a GPU fence value to complete
    void WaitForGpuFence(UINT64 fenceValue);
    
    // Present the swap chain
    void PresentSwapChain(Canvas::XGfxSwapChain* pSwapChain);

    // Schedule release of a host-write suballocation after the current GPU work completes.
    // The release is deferred until the GPU fence advances past the current value.
    void RetireUploadAllocation(const Canvas::GfxSuballocation& suballocation);
    
    //---------------------------------------------------------------------------------------------
    // GPU Task Graph API
    //
    // Tasks are GPU operations (render passes). Each declares its resource usage.
    // Barriers are resolved immediately when a task is prepared. Commands are recorded
    // directly into the command list by the caller — no deferred callbacks.
    //
    // Usage:
    //   auto task = CreateGpuTask("ShadowPass");
    //   DeclareGpuTextureUsage(task, pShadowMap, DEPTH_STENCIL_WRITE, ...);
    //   PrepareGpuTask(task);   // emits barriers into CL
    //   // record commands directly into CL...
    //---------------------------------------------------------------------------------------------
    
    // Begin a new GPU task graph for this frame.
    void BeginTaskGraph();
    
    // Create a GPU task within the current task graph
    Canvas::CGpuTask& CreateGpuTask(const char* name = nullptr);
    
    // Declare texture usage for a GPU task
    void DeclareGpuTextureUsage(
        Canvas::CGpuTask& task,
        ID3D12Resource* pResource,
        D3D12_BARRIER_LAYOUT requiredLayout,
        D3D12_BARRIER_SYNC sync,
        D3D12_BARRIER_ACCESS access,
        UINT subresources = 0xFFFFFFFF);
    
    // Declare buffer usage for a GPU task
    void DeclareGpuBufferUsage(
        Canvas::CGpuTask& task,
        ID3D12Resource* pResource,
        D3D12_BARRIER_SYNC sync,
        D3D12_BARRIER_ACCESS access,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX);
    
    // Prepare a GPU task: resolves barriers and emits them into the command list.
    void PrepareGpuTask(Canvas::CGpuTask& task);
    
    // Add an explicit dependency between GPU tasks
    void AddGpuTaskDependency(Canvas::CGpuTask& task, Canvas::CGpuTask& dependency);
    
    // Get the current task graph (for advanced usage)
    Canvas::CGpuTaskGraph& GetTaskGraph() { return m_GpuTaskGraph; }

    //---------------------------------------------------------------------------------------------
    // Resource-Aware Command Recording (with automatic barrier insertion)
    //
    // These methods enable callers to declare resource usage, eliminating the need for manual
    // barrier management. The RenderQueue automatically:
    // 1. Inserts barriers for layout/access/sync transitions
    // 2. Detects and prevents concurrent writes to the same resource
    //---------------------------------------------------------------------------------------------
    
    // Record commands with declared resource usage.
    // Automatically generates barriers based on current recording state, executes the
    // recording lambda, and updates state.
    //
    // Example:
    //   ResourceUsageBuilder usages;
    //   usages.BufferAsCopySource(srcBuffer).BufferAsCopyDest(dest);
    //   
    //   pQueue->RecordCommandBlock(
    //       usages.Build(),
    //       [](ID3D12GraphicsCommandList* pCL) {
    //           pCL->CopyResource(dest, src);
    //       });
    void RecordCommandBlock(
        const ResourceUsages& resourceUsages,
        std::function<void(ID3D12GraphicsCommandList*)> recordFunc,
        const char* name = nullptr);
    
    // Validate resource usage declarations for write conflicts
    bool ValidateResourceUsageNoWriteConflicts(const ResourceUsages& resourceUsages) const;
    
    // Get resource state snapshot for a specific resource (for debugging/analysis)
    struct ResourceStateSnapshot
    {
        std::optional<D3D12_BARRIER_LAYOUT> UniformLayout;
        std::unordered_map<UINT, D3D12_BARRIER_LAYOUT> PerSubresourceLayouts;
        
        D3D12_BARRIER_LAYOUT GetLayout(UINT subresource = 0xFFFFFFFF) const {
            if (UniformLayout.has_value()) {
                return UniformLayout.value();
            }
            if (subresource == 0xFFFFFFFF) {
                return D3D12_BARRIER_LAYOUT_COMMON;
            }
            auto it = PerSubresourceLayouts.find(subresource);
            return (it != PerSubresourceLayouts.end()) ? it->second : D3D12_BARRIER_LAYOUT_COMMON;
        }
    };
    
    ResourceStateSnapshot GetResourceState(ID3D12Resource* pResource) const;
    
    // Process completed GPU work (release deferred resources)
    void ProcessCompletedWork();

private:
    // Ensure the task graph is active (lazy initialization)
    void EnsureTaskGraphActive();
    
    // Emit resolved barriers into the scene command list
    void EmitBarriers(const Canvas::TaskBarriers& barriers);

    // Emit resolved barriers into an arbitrary command list
    void EmitBarriersToCommandList(ID3D12GraphicsCommandList7* pCL7, const Canvas::TaskBarriers& barriers);

    // GPU Task Graph instances — one per command list context
    Canvas::CGpuTaskGraph m_GpuTaskGraph;      // Scene CL
    Canvas::CGpuTaskGraph m_UIGpuTaskGraph;     // UI CL
    bool m_TaskGraphActive = false;
};
