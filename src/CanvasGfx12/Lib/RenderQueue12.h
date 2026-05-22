//================================================================================================
// Context
//================================================================================================

#pragma once

#include "CanvasGfx12.h"
#include "GpuTask.h"
#include "HlslTypes.h"
#include "CommandAllocatorPool.h"
#include "ResourceManager.h"
#include "UploadRing.h"

#ifdef _MSC_VER
#pragma warning(push)
// 4324: 'CRenderQueue12': structure padded due to alignment specifier. Triggered
// by std::vector<DisplacedDrawDesc> embedding a FloatMatrix4x4 (alignas(16)).
#pragma warning(disable: 4324)
#endif

// Forward declarations
class CSurface12;
class CSwapChain12;

// Private representation of one displaced patch-grid draw queued by
// CRenderQueue12::DrawMesh during scene walk and drained at EndFrame.
// Carries everything the GPU-tessellated drain needs: GPU resources,
// LOD knobs (read from the material's GfxDisplacementDesc), per-tile
// extents (also from the displacement desc + the node transform), and
// the patch grid dim derived from the procedural mesh's vertex count.
// Engine-internal only; not visible through any public API.
struct DisplacedDrawDesc
{
    Canvas::XGfxSurface     *pHeightmap     = nullptr;
    Canvas::XGfxSurface     *pAlbedo        = nullptr;
    Canvas::XGfxSurface     *pAOMap         = nullptr;
    Canvas::XGfxSurface     *pRoughnessMap  = nullptr;
    Canvas::Math::FloatMatrix4x4 World;
    float    OriginX       = 0.0f;
    float    OriginY       = 0.0f;
    float    WorldSizeX    = 0.0f;
    float    WorldSizeY    = 0.0f;
    float    HeightScale   = 0.0f;
    float    HeightBias    = 0.0f;
    uint32_t PatchGridDim  = 64;
};

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
    CSurface12* pSurface = nullptr;
    
    // Layout state during this task's execution
    D3D12_BARRIER_LAYOUT RequiredLayout = D3D12_BARRIER_LAYOUT_COMMON;
    
    // GPU sync/access needed for this usage - this is the source of truth for what operation is happening
    D3D12_BARRIER_SYNC SyncForUsage = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_ACCESS AccessForUsage = D3D12_BARRIER_ACCESS_NO_ACCESS;
    
    // Granularity control
    UINT Subresources = 0xFFFFFFFF;  // All subresources by default
    D3D12_TEXTURE_BARRIER_FLAGS Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
    
    // Validation helpers - infer from D3D12_BARRIER_ACCESS
    bool IsValid() const { return pSurface != nullptr && AccessForUsage != D3D12_BARRIER_ACCESS_NO_ACCESS; }
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
    CBuffer12* pBuffer = nullptr;
    
    // GPU sync/access needed for this usage - this is the source of truth for what operation is happening
    D3D12_BARRIER_SYNC SyncForUsage = D3D12_BARRIER_SYNC_NONE;
    D3D12_BARRIER_ACCESS AccessForUsage = D3D12_BARRIER_ACCESS_NO_ACCESS;
    
    // Granularity control
    UINT64 Offset = 0;
    UINT64 Size = UINT64_MAX;  // Whole resource by default
    
    // Validation helpers - infer from D3D12_BARRIER_ACCESS
    bool IsValid() const { return pBuffer != nullptr && AccessForUsage != D3D12_BARRIER_ACCESS_NO_ACCESS; }
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
                if (TextureUsages[i].pSurface == TextureUsages[j].pSurface)
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
                if (BufferUsages[i].pBuffer == BufferUsages[j].pBuffer)
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
        CSurface12* pSurface,
        D3D12_BARRIER_LAYOUT requiredLayout,
        D3D12_BARRIER_SYNC syncForUsage,
        D3D12_BARRIER_ACCESS accessForUsage,
        UINT subresources = 0xFFFFFFFF)
    {
        TextureUsage usage;
        usage.pSurface = pSurface;
        usage.RequiredLayout = requiredLayout;
        usage.SyncForUsage = syncForUsage;
        usage.AccessForUsage = accessForUsage;
        usage.Subresources = subresources;
        m_usages.AddTextureUsage(usage);
        return *this;
    }
    
    // Convenience: Texture as shader resource (read-only)
    ResourceUsageBuilder& TextureAsShaderResource(
        CSurface12* pSurface,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pSurface,
            D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_PIXEL_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
            subresources);
    }
    
    // Convenience: Texture as unordered access view (read-write)
    ResourceUsageBuilder& TextureAsUnorderedAccess(
        CSurface12* pSurface,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pSurface,
            D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING,
            D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
            subresources);
    }
    
    // Convenience: Texture as render target
    ResourceUsageBuilder& TextureAsRenderTarget(
        CSurface12* pSurface,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pSurface,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            D3D12_BARRIER_SYNC_RENDER_TARGET,
            D3D12_BARRIER_ACCESS_RENDER_TARGET,
            subresources);
    }
    
    // Convenience: Texture as depth-stencil target (write)
    ResourceUsageBuilder& TextureAsDepthStencilWrite(
        CSurface12* pSurface,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pSurface,
            D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
            D3D12_BARRIER_SYNC_DEPTH_STENCIL,
            D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
            subresources);
    }
    
    // Convenience: Texture as depth-stencil resource (read)
    ResourceUsageBuilder& TextureAsDepthStencilRead(
        CSurface12* pSurface,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pSurface,
            D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ,
            D3D12_BARRIER_SYNC_DEPTH_STENCIL,
            D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ,
            subresources);
    }
    
    // Convenience: Texture as copy destination
    ResourceUsageBuilder& TextureAsCopyDest(
        CSurface12* pSurface,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pSurface,
            D3D12_BARRIER_LAYOUT_COMMON,
            D3D12_BARRIER_SYNC_COPY,
            D3D12_BARRIER_ACCESS_COPY_DEST,
            subresources);
    }
    
    // Convenience: Texture as copy source (read-only)
    ResourceUsageBuilder& TextureAsCopySource(
        CSurface12* pSurface,
        UINT subresources = 0xFFFFFFFF)
    {
        return SetTextureUsage(
            pSurface,
            D3D12_BARRIER_LAYOUT_COMMON,
            D3D12_BARRIER_SYNC_COPY,
            D3D12_BARRIER_ACCESS_COPY_SOURCE,
            subresources);
    }

    //---------------------------------------------------------------------------------------------
    // Primary buffer usage method - explicitly specify all parameters
    //---------------------------------------------------------------------------------------------
    ResourceUsageBuilder& SetBufferUsage(
        CBuffer12* pBuffer,
        D3D12_BARRIER_SYNC syncForUsage,
        D3D12_BARRIER_ACCESS accessForUsage,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        BufferUsage usage;
        usage.pBuffer = pBuffer;
        usage.SyncForUsage = syncForUsage;
        usage.AccessForUsage = accessForUsage;
        usage.Offset = offset;
        usage.Size = size;
        m_usages.AddBufferUsage(usage);
        return *this;
    }
    
    // Convenience: Buffer as shader resource (read-only)
    ResourceUsageBuilder& BufferAsShaderResource(
        CBuffer12* pBuffer,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pBuffer,
            D3D12_BARRIER_SYNC_PIXEL_SHADING,
            D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
            offset,
            size);
    }
    
    // Convenience: Buffer as unordered access (read-write)
    ResourceUsageBuilder& BufferAsUnorderedAccess(
        CBuffer12* pBuffer,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pBuffer,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING,
            D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
            offset,
            size);
    }
    
    // Convenience: Buffer as constant buffer (read-only)
    ResourceUsageBuilder& BufferAsConstantBuffer(
        CBuffer12* pBuffer,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pBuffer,
            D3D12_BARRIER_SYNC_PIXEL_SHADING,
            D3D12_BARRIER_ACCESS_CONSTANT_BUFFER,
            offset,
            size);
    }
    
    // Convenience: Buffer as vertex buffer (read-only)
    ResourceUsageBuilder& BufferAsVertexBuffer(
        CBuffer12* pBuffer,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pBuffer,
            D3D12_BARRIER_SYNC_VERTEX_SHADING,
            D3D12_BARRIER_ACCESS_VERTEX_BUFFER,
            offset,
            size);
    }
    
    // Convenience: Buffer as index buffer (read-only)
    ResourceUsageBuilder& BufferAsIndexBuffer(
        CBuffer12* pBuffer,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pBuffer,
            D3D12_BARRIER_SYNC_INDEX_INPUT,
            D3D12_BARRIER_ACCESS_INDEX_BUFFER,
            offset,
            size);
    }
    
    // Convenience: Buffer as copy destination
    ResourceUsageBuilder& BufferAsCopyDest(
        CBuffer12* pBuffer,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pBuffer,
            D3D12_BARRIER_SYNC_COPY,
            D3D12_BARRIER_ACCESS_COPY_DEST,
            offset,
            size);
    }
    
    // Convenience: Buffer as copy source (read-only)
    ResourceUsageBuilder& BufferAsCopySource(
        CBuffer12* pBuffer,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX)
    {
        return SetBufferUsage(
            pBuffer,
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

    // Shared allocator pool for all task graphs
    CCommandAllocatorPool m_AllocatorPool;
    bool m_UICommandListOpen = false;
    CComPtr<ID3D12DescriptorHeap> m_pShaderResourceDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pSamplerDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pRTVDescriptorHeap;
    CComPtr<ID3D12DescriptorHeap> m_pDSVDescriptorHeap;
    CComPtr<ID3D12RootSignature> m_pDefaultRootSig;
    CComPtr<ID3D12PipelineState> m_pDefaultPSO;
    CComPtr<ID3D12PipelineState> m_pDefaultPSOWireframe;  // built lazily on first wireframe enable
    bool                          m_GeometryWireframe = false;
    CComPtr<ID3D12RootSignature> m_pDisplacedRootSig;
    CComPtr<ID3D12PipelineState> m_pDisplacedPSO;
    CComPtr<ID3D12PipelineState> m_pDisplacedPSOWireframe;
    // Depth-only displaced PSO + root sig, used for shadow-atlas tile
    // rendering.  Reuses VSDisplaced + HSDisplaced bytecode (so shadow
    // tessellation tracks camera LOD and avoids Peter Panning) and
    // pairs them with DSDisplacedShadow + no PS.  Created lazily on
    // first shadow-casting light submission.
    CComPtr<ID3D12RootSignature> m_pDisplacedShadowRootSig;
    CComPtr<ID3D12PipelineState> m_pDisplacedShadowPSO;
    CComPtr<ID3D12RootSignature> m_pTextRootSig;
    CComPtr<ID3D12PipelineState> m_pTextPSO;
    DXGI_FORMAT m_TextPSOFormat = DXGI_FORMAT_UNKNOWN;
    CComPtr<ID3D12RootSignature> m_pRectRootSig;
    CComPtr<ID3D12PipelineState> m_pRectPSO;
    DXGI_FORMAT m_RectPSOFormat = DXGI_FORMAT_UNKNOWN;
    UINT64 m_FenceValue = 0;
    CComPtr<ID3D12Fence> m_pFence;
    Gem::TGemPtr<CDevice12> m_pDevice;  // ref-counted; device outlives render queue

    // Stable id assigned by CDevice12::GetResourceManager().RegisterTimeline().
    // Combined with m_FenceValue this forms a FenceToken usable for queue-agnostic
    // deferred operations (RetireBuffer, DeferRelease).
    uint32_t m_TimelineId = FenceToken::kInvalidTimelineId;

    uint32_t GetTimelineId() const { return m_TimelineId; }
    FenceToken MakeFenceToken() const { return FenceToken{ m_TimelineId, m_FenceValue }; }

    // Per-queue upload ring for host-write staging (UPLOAD heap).
    // Private to this queue — only touched by this queue's thread — so its
    // fence-value markers are unambiguous bare UINT64s on this fence.
    CUploadRing m_UploadRing;

    CUploadRing& GetUploadRing() { return m_UploadRing; }

    // Depth buffer for rendering
    Gem::TGemPtr<CSurface12> m_pDepthBuffer;
    UINT m_DepthBufferWidth = 0;
    UINT m_DepthBufferHeight = 0;
    UINT m_NextDSVSlot = 0;

    // Shadow atlas.  One engine-internal R32_TYPELESS surface shared by
    // every shadow-casting directional light this frame.  Sub-allocated
    // into a fixed 2x2 grid of square tiles (so up to 4 directional
    // shadow casters at the same time); each tile is selected via the
    // light's atlas-rect-UV in HlslLight.  Created lazily by
    // EnsureShadowAtlas() the first frame any enabled shadow-casting
    // light is submitted.
    //
    //   m_pShadowAtlas        - committed resource holding the depth data.
    //   m_ShadowAtlasSize     - atlas side length in texels (square).
    //   m_ShadowAtlasTilesPerSide - grid resolution (2 -> 2x2 = 4 tiles).
    //   m_ShadowAtlasDSV      - cached DSV referencing the whole atlas;
    //                           individual tiles select their region via
    //                           viewport / scissor / clear rect.
    //   m_NextShadowTileIndex - per-frame allocator counter; reset to 0
    //                           at EndFrame start.
    Gem::TGemPtr<CSurface12>     m_pShadowAtlas;
    UINT                         m_ShadowAtlasSize         = 0;
    UINT                         m_ShadowAtlasTilesPerSide = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_ShadowAtlasDSV          = {};
    UINT                         m_NextShadowTileIndex     = 0;

    static const UINT kShadowAtlasDefaultSize     = 4096;
    static const UINT kShadowAtlasTilesPerSide    = 2;     // 2 -> 4 tiles
    static const UINT kShadowAtlasMaxTiles        = kShadowAtlasTilesPerSide * kShadowAtlasTilesPerSide;

    // One allocated shadow tile.  Pixel coordinates index into the atlas;
    // AtlasRectUV is the same region expressed in [0,1] atlas UV (xy =
    // origin, zw = size) and is what the shader sees via HlslLight.
    struct ShadowAtlasTile
    {
        bool                         Valid;
        UINT                         PixelX;
        UINT                         PixelY;
        UINT                         PixelSize;
        Canvas::Math::FloatVector4   AtlasRectUV;
    };

    // G-buffer render targets for deferred shading
    Gem::TGemPtr<CSurface12> m_pGBufferNormals;
    Gem::TGemPtr<CSurface12> m_pGBufferDiffuseColor;
    Gem::TGemPtr<CSurface12> m_pGBufferWorldPos;
    Gem::TGemPtr<CSurface12> m_pGBufferPBR;       // R=Roughness, G=Metallic, B=AO, A=spare
    Gem::TGemPtr<CSurface12> m_pGBufferEmissive;  // RGB linear emissive
    Canvas::GfxFormat m_GBufferNormalsFormat  = Canvas::GfxFormat::R10G10B10A2_UNorm;
    Canvas::GfxFormat m_GBufferDiffuseFormat  = Canvas::GfxFormat::R10G10B10A2_UNorm;
    Canvas::GfxFormat m_GBufferWorldPosFormat = Canvas::GfxFormat::R16G16B16A16_Float;
    Canvas::GfxFormat m_GBufferPBRFormat      = Canvas::GfxFormat::R8G8B8A8_UNorm;
    Canvas::GfxFormat m_GBufferEmissiveFormat = Canvas::GfxFormat::R11G11B10_Float;
    UINT m_GBufferWidth = 0;
    UINT m_GBufferHeight = 0;

    bool m_GBufferDescriptorsDirty = true;

    // Composition pass PSO and root signature
    CComPtr<ID3D12PipelineState> m_pCompositePSO;
    CComPtr<ID3D12RootSignature> m_pCompositeRootSig;

    // Frame rendering state
    CSwapChain12 *m_pCurrentSwapChain = nullptr;   // Set during BeginFrame..EndFrame
    Canvas::XCamera *m_pActiveCamera = nullptr;     // Set by scene via SetActiveCamera during Update

    // Scene background.  Populated by SetBackground (typically forwarded
    // from XScene at the start of each submission); the cubemap surfaces
    // are held via strong refs that parallel the raw pointers in
    // m_Background so the surfaces stay alive while bound.  When no
    // background has been set the default is opaque black with no skybox.
    Canvas::GfxBackgroundDesc m_Background;
    Gem::TGemPtr<Canvas::XGfxSurface> m_pSkyCubeA;
    Gem::TGemPtr<Canvas::XGfxSurface> m_pSkyCubeB;
    Gem::TGemPtr<Canvas::XGfxSurface> m_pStarsCube;
    Gem::TGemPtr<Canvas::XGfxSurface> m_pMoonTexture;
    HlslTypes::HlslLight m_Lights[MAX_LIGHTS_PER_REGION] = {};
    uint32_t m_LightCount = 0;

    // Pending shadow-caster work, accumulated by SubmitLight when an
    // enabled directional light has LightFlags::CastsShadows.  Drained
    // at EndFrame: each entry generates one ShadowPass GpuTask per
    // pending displaced draw.  Cleared at EndFrame epilogue alongside
    // m_LightCount.
    struct PendingShadowCaster
    {
        uint32_t                     LightSlotIndex;  // index into m_Lights[]
        Canvas::Math::FloatMatrix4x4 ShadowViewProj;
        UINT                         TilePixelX;
        UINT                         TilePixelY;
        UINT                         TilePixelSize;
    };
    std::vector<PendingShadowCaster> m_PendingShadowCasters;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_CurrentRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE  m_CurrentDSV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE  m_GBufferRTVs[5] = {};

    // Back-buffer viewport + scissor, captured at BeginFrame so passes
    // that temporarily switch to a different render target (e.g. shadow
    // atlas tiles) can restore them and keep downstream draws on the
    // intended visible surface.
    D3D12_VIEWPORT               m_BackBufferViewport = {};
    D3D12_RECT                   m_BackBufferScissor  = {};

    // Renderable nodes enqueued during scene graph update, dispatched during EndFrame
    std::vector<Canvas::XSceneGraphNode*> m_RenderableQueue;

    // Pending displaced-mesh submissions, drained during EndFrame.
    std::vector<DisplacedDrawDesc> m_DisplacedDraws;

    // UI graph nodes enqueued during UI graph submission, dispatched during EndFrame
    std::vector<Canvas::XUIGraphNode*> m_UIRenderableQueue;

    // SRV descriptor allocation for per-draw structured buffers
    UINT m_NextSRVSlot = 0;

    static const UINT NumShaderResourceDescriptors = 65536;
    static const UINT NumSamplerDescriptors = 1024;
    static const UINT NumRTVDescriptors = 64;
    static const UINT NumDSVDescriptors = 64;

    UINT m_NextRTVSlot = 0;

    UINT m_CbvSrvUavIncrement = 0;
    UINT m_SamplerIncrement   = 0;
    UINT m_RtvIncrement       = 0;
    UINT m_DsvIncrement       = 0;

    // { shader-resource heap, sampler heap } cache for SetDescriptorHeaps.
    ID3D12DescriptorHeap* m_DescriptorHeapsArray[2] = {};

    // GPU sync point tracking (fence-value based)
    std::unordered_map<UINT64, GpuSyncPoint> m_GpuSyncPoints;

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
    GEMMETHOD(FlushAndPresent)(Canvas::XGfxSwapChain *pSwapChain) final;
    GEMMETHOD(BeginFrame)(Canvas::XGfxSwapChain *pSwapChain) final;
    GEMMETHOD(SubmitForRender)(Canvas::XSceneGraphNode *pNode) final;
    GEMMETHOD(SubmitForUIRender)(Canvas::XUIGraphNode *pNode) final;
    GEMMETHOD_(void, SetActiveCamera)(Canvas::XCamera *pCamera) final;
    GEMMETHOD_(void, SetBackground)(const Canvas::GfxBackgroundDesc *pDesc) final;
    GEMMETHOD_(void, SetGeometryWireframe)(bool wireframe) final;
    GEMMETHOD_(bool, GetGeometryWireframe)() const final;
    GEMMETHOD(FinalizeUploadAsShaderResource)(Canvas::XGfxSurface *pSurface) final;
    GEMMETHOD(EndFrame)() final;

    // Internal: queue a displaced-mesh draw, called by DrawMesh when it
    // detects a procedural patch-grid mesh paired with a displacement-
    // enabled material. Not part of the public interface.
    Gem::Result SubmitDisplacedDraw(const DisplacedDrawDesc &desc);

    // Internal functions
    CDevice12 *GetDevice() const { return m_pDevice; }
    ID3D12CommandQueue *GetD3DCommandQueue() { return m_pCommandQueue; }
    UINT64 GetFenceValue() const { return m_FenceValue; }

    // Internal mesh drawing (called from EndFrame, not on any public interface)
    Gem::Result DrawMesh(Canvas::XGfxMeshData *pMeshData, uint32_t materialGroupIndex, const Canvas::Math::FloatMatrix4x4 &worldTransform);

    // Flush: compute final layouts, update committed state, close/submit CL
    void Flush();

    D3D12_CPU_DESCRIPTOR_HANDLE CreateRenderTargetView(class CSurface12 *pSurface, UINT ArraySlice, UINT MipSlice, UINT PlaneSlice);
    D3D12_CPU_DESCRIPTOR_HANDLE CreateDepthStencilView(class CSurface12 *pSurface);
    
    // Create or resize the depth buffer to match the given dimensions
    void EnsureDepthBuffer(UINT width, UINT height);

    // Create or resize G-buffer render targets to match the given dimensions
    void EnsureGBuffers(UINT width, UINT height);

    // Create the shadow atlas surface + DSV the first time a shadow-casting
    // light needs one this frame.  Idempotent; safe to call every frame.
    void EnsureShadowAtlas();

    // Allocate the next shadow tile from the atlas grid for a light that
    // wants up to `preferredResolution` texels per side.  Tile size is
    // clamped to the fixed grid cell (atlas side / tiles-per-side).
    // Returns Valid=false when the atlas is full this frame; caller should
    // then leave the light's HlslLight.ShadowAtlasRectUV zeroed so the
    // composite skips its shadow sample.
    ShadowAtlasTile AllocateShadowTile(UINT preferredResolution);
    
    // Create the default (geometry pass) PSO (lazily, on first use)
    void EnsureDefaultPSO();

    // Lazily create the wireframe variant of the geometry pass PSO.
    void EnsureDefaultPSOWireframe();

    // Lazily create the displaced-mesh root sig + PSO variants (solid + wireframe).
    void EnsureDisplacedPSO();
    void EnsureDisplacedPSOWireframe();

    // Lazily create the depth-only displaced shadow PSO + root sig used
    // for rendering shadow-atlas tiles.  Idempotent.
    void EnsureDisplacedShadowPSO();

    ID3D12PipelineState* GetActiveDisplacedPSO()
    {
        if (m_GeometryWireframe)
        {
            EnsureDisplacedPSOWireframe();
            return m_pDisplacedPSOWireframe;
        }
        EnsureDisplacedPSO();
        return m_pDisplacedPSO;
    }

    // Pick the appropriate variant of the default geometry pass PSO based
    // on the current wireframe debug flag.
    ID3D12PipelineState* GetActiveDefaultPSO()
    {
        if (m_GeometryWireframe)
        {
            EnsureDefaultPSOWireframe();
            return m_pDefaultPSOWireframe;
        }
        EnsureDefaultPSO();
        return m_pDefaultPSO;
    }

    // Create the composition PSO and root signature (lazily, on first use)
    void EnsureCompositePSO(DXGI_FORMAT rtvFormat);
    
    // Create the text PSO and root signature (lazily, on first use)
    void EnsureTextPSO(DXGI_FORMAT rtvFormat);

    // Create the rect PSO and root signature (lazily, on first use)
    void EnsureRectPSO(DXGI_FORMAT rtvFormat);

    void FlushPendingGlyphUploads();

    // Add a GPU resource ref for the current frame (released after fence completes)
    void DeferRelease(Gem::XGeneric* pResource);
    Gem::Result UploadTextureRegion(Canvas::XGfxSurface *pDstSurface, uint32_t dstX, uint32_t dstY, uint32_t width, uint32_t height, const void *pData, uint32_t srcRowPitch);
    // Internal UI element drawing (mirrors DrawMesh pattern)
    Gem::Result DrawUIText(const Canvas::GfxResourceAllocation& glyphSRV, Canvas::XGfxSurface* pGlyphAtlas, const Canvas::Math::FloatVector4& textColor, const Canvas::Math::FloatVector2& elementOffset);
    Gem::Result DrawUIRect(const Canvas::Math::FloatVector2& rectSize, const Canvas::Math::FloatVector4& fillColor, const Canvas::Math::FloatVector2& elementOffset);
    
    // Allocate a shader-visible SRV descriptor slot and return GPU handle
    D3D12_GPU_DESCRIPTOR_HANDLE CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);
    
    // Signal the GPU fence and record a sync point
    void CreateGpuSyncPoint(UINT64 fenceValue);
    
    // Wait on CPU for a GPU fence value to complete
    void WaitForGpuFence(UINT64 fenceValue);
    
    // Present the swap chain
    void PresentSwapChain(Canvas::XGfxSwapChain* pSwapChain);

    //---------------------------------------------------------------------------------------------
    // GPU Task Graph API
    //
    // Tasks are GPU operations (render passes). Each has a recording function and declares
    // resource usage. InsertGpuTask resolves barriers, emits them into the work CL, and
    // invokes the recording function — all atomically.
    //
    // Usage:
    //   auto& task = CreateGpuTask("ShadowPass");
    //   DeclareGpuTextureUsage(task, pShadowMap, DEPTH_STENCIL_WRITE, ...);
    //   task.RecordFunc = [](ID3D12GraphicsCommandList* pCL) { ... };
    //   InsertGpuTask(task);
    //---------------------------------------------------------------------------------------------
    
    // Create a GPU task within the current task graph
    Canvas::CGpuTask& CreateGpuTask(const char* name = nullptr);
    
    // Declare texture usage for a GPU task
    void DeclareGpuTextureUsage(
        Canvas::CGpuTask& task,
        CSurface12* pSurface,
        D3D12_BARRIER_LAYOUT requiredLayout,
        D3D12_BARRIER_SYNC sync,
        D3D12_BARRIER_ACCESS access,
        UINT subresources = 0xFFFFFFFF);
    
    // Declare buffer usage for a GPU task
    void DeclareGpuBufferUsage(
        Canvas::CGpuTask& task,
        CBuffer12* pBuffer,
        D3D12_BARRIER_SYNC sync,
        D3D12_BARRIER_ACCESS access,
        UINT64 offset = 0,
        UINT64 size = UINT64_MAX);
    
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
    
    ResourceStateSnapshot GetResourceState(CSurface12* pSurface) const;
    
    // Process completed GPU work (release deferred resources)
    void ProcessCompletedWork();

private:
    // Ensure the task graph is active (lazy initialization)
    void EnsureTaskGraphActive();

    // Accumulate a light for the current frame's deferred lighting pass
    Gem::Result SubmitLight(Canvas::XLight *pLight);

    // Build a texel-snapped world-to-shadow-clip matrix for a directional
    // light.  The shadow frustum is an ortho box of side 2 * halfWidth in
    // light-space XY, depth depthRange in light-space Z, centred on the
    // camera position projected into light space and snapped to the
    // shadow-atlas texel grid.  Texel snapping is what stops shadow edges
    // from shimmering as the camera translates.
    //
    //   lightDir       - unit world-space direction the light emits along.
    //   worldUp        - reference up vector (typically scene Z-up); used
    //                    only to disambiguate the light-space basis.  Must
    //                    not be parallel to lightDir.
    //   focusPoint     - world-space point the shadow frustum is centred on
    //                    (typically the active camera position).
    //   halfWidth      - half side length of the ortho box in meters.
    //   depthRange     - light-space Z extent of the ortho box in meters.
    //   resolution     - shadow map resolution in texels (used for snapping).
    //
    // The returned matrix takes world-space row vectors directly to shadow
    // clip space (reverse-Z: 1.0 at near, 0.0 at far).
    static Canvas::Math::FloatMatrix4x4 BuildDirectionalShadowMatrix(
        const Canvas::Math::FloatVector4& lightDir,
        const Canvas::Math::FloatVector4& worldUp,
        const Canvas::Math::FloatVector4& focusPoint,
        float halfWidth,
        float depthRange,
        UINT  resolution);
    
    // GPU Task Graphs — three graphs dispatched in order: scene → UI → present
    Canvas::CGpuTaskGraph m_GpuTaskGraph;        // Scene: geometry, composite
    Canvas::CGpuTaskGraph m_UIGpuTaskGraph;       // UI: text, HUD overlays
    Canvas::CGpuTaskGraph m_PresentGpuTaskGraph;  // Present: back buffer -> COMMON
    bool m_TaskGraphActive = false;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
