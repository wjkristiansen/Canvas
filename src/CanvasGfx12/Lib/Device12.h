//================================================================================================
// Device12 - Example of external Canvas element registration
//
// This class demonstrates the proper pattern for external plugins:
// 1. Inherit from XGfxDevice (which inherits from XCanvasElement)
// 2. Implement XCanvasElement::Register/Unregister methods
// 3. External callers use device->Register(canvas), NOT canvas->RegisterElement(device)
// 4. Only inside Register/Unregister implementations should canvas->RegisterElement be called
//================================================================================================

#pragma once

#include "D3D12ResourceUtils.h"
#include "CanvasGfx12.h"
#include "BuddySuballocator.h"
#include "ResourceAllocator.h"
#include "ResourceManager.h"
#include "UploadRing.h"
#include "CopyQueue.h"
#include "GlyphAtlas.h"
#include "DescriptorHeapAllocator.h"

inline constexpr D3D12_HEAP_TYPE GfxMemoryUsageToD3D12HeapType(Canvas::GfxMemoryUsage usage)
{
    switch (usage)
    {
    case Canvas::GfxMemoryUsage::DeviceLocal:
        return D3D12_HEAP_TYPE_DEFAULT;
    case Canvas::GfxMemoryUsage::HostWrite:
        return D3D12_HEAP_TYPE_UPLOAD;
    case Canvas::GfxMemoryUsage::HostRead:
        return D3D12_HEAP_TYPE_READBACK;
    default:
        return D3D12_HEAP_TYPE_DEFAULT;
    }
}

//------------------------------------------------------------------------------------------------
class CDevice12 : public TGfxElement<Canvas::XGfxDevice>
{
public:
    CComPtr<ID3D12Device10> m_pD3DDevice;

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxDevice)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CDevice12(Canvas::XCanvas* pCanvas, PCSTR name = nullptr);
    ~CDevice12();

    Gem::Result Initialize();
    void Uninitialize() {}

    // XGfxDevice methods
    GEMMETHOD(CreateRenderQueue)(Canvas::XGfxRenderQueue **ppRenderQueue) final;
    GEMMETHOD(CreateMaterial)(Canvas::XGfxMaterial **ppMaterial) final;
    GEMMETHOD(CreateSurface)(const Canvas::GfxSurfaceDesc &desc, Canvas::XGfxSurface **ppSurface) final;
    GEMMETHOD(CreateBuffer)(uint64_t sizeInBytes, Canvas::GfxMemoryUsage memoryUsage, Canvas::XGfxBuffer **ppBuffer) final;
    GEMMETHOD(CreateMeshData)(const Canvas::MeshDataDesc &desc, Canvas::XGfxMeshData **ppMesh) final;
    GEMMETHOD(CreateDebugMeshData)(
        uint32_t vertexCount,
        const Canvas::Math::FloatVector4 *positions,
        const Canvas::Math::FloatVector4 *normals,
        Canvas::XGfxMeshData **ppMesh,
        const char* name = nullptr) final;

    // Bring the inherited non-virtual single-stream wrapper into scope so
    // overload resolution finds it on a CDevice12* (the descriptor-form
    // override above would otherwise hide it).
    using Canvas::XGfxDevice::CreateMeshData;

    ID3D12Device10 *GetD3DDevice() const { return m_pD3DDevice; }

    Canvas::XLogger *GetLogger()
    {
        auto pCanvas = GetCanvas();
        return pCanvas ? pCanvas->GetLogger() : nullptr;
    }

#if defined(_DEBUG)
    // Cached ref-counted logger for the debug layer callback. The callback
    // may fire from internal D3D12 threads, so we avoid reaching through
    // m_pCanvas (which can be cleared without synchronisation).
    Gem::TGemPtr<Canvas::XLogger> m_pDebugLogger;
    CComPtr<ID3D12InfoQueue1> m_pInfoQueue1;
    DWORD m_debugCallbackCookie = 0;
#endif

    // Device-level resource manager (queue-agnostic): owns the resource allocator
    // (placed + committed), the bucketed buffer pool, and per-timeline retired/
    // deferred queues.
    CResourceManager m_ResourceManager;

    CResourceManager& GetResourceManager() { return m_ResourceManager; }

    // Device-owned COPY queue used for host->device buffer uploads (mesh data,
    // vertex buffers, ...).  Render queues query EnsureUploadsRetired() at submit
    // time to obtain a fence token to Wait() on before consuming the destinations.
    CCopyQueue m_CopyQueue;

    CCopyQueue& GetCopyQueue() { return m_CopyQueue; }

    // Flush any pending upload work on the device's copy queue and return the
    // FenceToken consumers must Wait() on before reading the destinations.
    // Returns nullopt when no upload work is pending — the fast path on every
    // frame after the first.
    std::optional<FenceToken> EnsureUploadsRetired() { return m_CopyQueue.FlushIfPending(); }

    // Vertex buffer suballocation (XGfxDevice interface — alloc + upload)
    GEMMETHOD(AllocateStructuredBuffer)(uint32_t elementCount, uint32_t elementStride, const void* pInitialData, Canvas::XGfxRenderQueue* pRQ, Canvas::GfxResourceAllocation& inOut) final;

    GEMMETHOD(FlushUploads)() final;

    // Texture upload (XGfxDevice interface — copy queue staging)
    GEMMETHOD(UploadTextureRegion)(
        Canvas::XGfxSurface *pDstSurface,
        uint32_t subresourceIndex,
        uint32_t dstX, uint32_t dstY,
        uint32_t width, uint32_t height,
        const void *pData,
        uint32_t srcRowPitch) final;

    GEMMETHOD(CreateTextElement)(Canvas::XUITextElement **ppElement) final;
    GEMMETHOD(CreateRectElement)(Canvas::XUIRectElement **ppElement) final;

    GEMMETHOD_(void, SetDebugMessageSeverity)(Canvas::GfxDebugSeverity maxSeverity) final;

    Canvas::XGfxSurface* GetGlyphAtlasSurface();
    Canvas::CGlyphCache& GetGlyphCache() { return m_GlyphCache; }

    //--------------------------------------------------------------------------------------------
    // Shared shader-visible CBV/SRV/UAV descriptor heap.
    //
    // One heap per device so per-resource descriptors (mesh vertex-stream SRVs, material
    // texture SRVs) have a stable home that any render queue can bind by GPU handle.  The heap
    // is partitioned: a low persistent region [0, kNumPersistentSrvDescriptors) owned by
    // m_PersistentSrvAllocator (lifetime-scoped per-resource blocks), and a high transient
    // region that each render queue's CDescriptorRing recycles per frame for dynamic
    // per-draw descriptors.  Sizes are a tunable split of the fixed heap capacity.
    //--------------------------------------------------------------------------------------------
    static constexpr UINT kNumShaderResourceDescriptors = 65536;
    static constexpr UINT kNumPersistentSrvDescriptors  = 32768;
    static constexpr UINT kNumTransientSrvDescriptors   =
        kNumShaderResourceDescriptors - kNumPersistentSrvDescriptors;

    ID3D12DescriptorHeap* GetShaderResourceDescriptorHeap() const { return m_pShaderResourceDescriptorHeap; }
    UINT GetCbvSrvUavIncrement() const { return m_CbvSrvUavIncrement; }

    // Claim the shared heap's transient partition for a render queue's per-frame SRV ring.
    // Only one render queue per device is supported: a GPU has a single rendering engine, so
    // there is never a second render queue in practice, and the transient ring's fence-gated
    // reclaim is keyed to exactly one queue's fence.  The first call hands out the whole
    // transient partition; a second call throws (Gem::Result::Unavailable) rather than let two
    // rings hand out overlapping slots in the shared heap.  The owning queue releases the
    // claim at teardown.
    //
    // FUTURE (compute queues): physics / work-graph / other compute workloads are expected to
    // address resources through the persistent region (stable per-resource indices) or through
    // root descriptors (GPU VA, no heap slot) -- NOT this transient ring.  If a compute queue
    // ever genuinely needs transient heap slots, give it its own disjoint sub-range; do not
    // share this ring.  A shared ring would let a long-running compute submission stall
    // rendering (the ring blocks on a fence to reclaim slots) and could set up a fence lock
    // inversion (render waiting on compute while compute waits on render).  Keep the render
    // and compute transient lifetimes independent.
    void AcquireTransientSrvRange(UINT& baseSlotOut, UINT& countOut);
    void ReleaseTransientSrvRange() { m_TransientSrvRangeClaimed = false; }

    // Persistent per-resource descriptor blocks.  Allocate returns an absolute heap slot (or
    // CDescriptorHeapAllocator::kInvalidSlot when the persistent region is exhausted); Free
    // returns the run.  GetSrv*Handle map a slot to a CPU handle (to write the descriptor) or
    // a GPU handle (to bind the table).
    UINT AllocatePersistentDescriptors(UINT count) { return m_PersistentSrvAllocator.Allocate(count); }
    void FreePersistentDescriptors(UINT baseSlot, UINT count) { m_PersistentSrvAllocator.Free(baseSlot, count); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetSrvCpuHandle(UINT slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(UINT slot) const;

    // Write one persistent descriptor at absolute heap slot `slot`.  A null resource writes a
    // null SRV of the matching kind so the table slot stays well-defined.  Used by meshes and
    // materials to populate their persistent per-resource descriptor blocks.
    void WriteStructuredBufferSRV(UINT slot, class CBuffer12* pBuffer, UINT stride);
    void WriteTexture2DSRV(UINT slot, class CSurface12* pSurface);

    // Sizes of a per-mesh-group vertex-stream descriptor block and a per-material texture
    // descriptor block.  The slot-to-stream / slot-to-role mappings are defined where the blocks
    // are filled (CDevice12::CreateMeshData and CMaterial12::PopulateDescriptors) and matched by
    // the default root signature in CRenderQueue12.
    static constexpr UINT kMeshStreamDescriptorCount = 5;
    static constexpr UINT kMaterialDescriptorCount   = 6;

    // GPU handle of the shared all-null material texture block, bound by DrawMesh for groups that
    // legitimately have no material (reserved once in InitializeDescriptorHeap).
    D3D12_GPU_DESCRIPTOR_HANDLE GetDefaultMaterialTableHandle();

private:
    // Create the shared shader-visible heap and initialize the persistent allocator.
    void InitializeDescriptorHeap();

    CComPtr<ID3D12DescriptorHeap> m_pShaderResourceDescriptorHeap;
    UINT m_CbvSrvUavIncrement = 0;
    CDescriptorHeapAllocator m_PersistentSrvAllocator;
    bool m_TransientSrvRangeClaimed = false;  // see AcquireTransientSrvRange
    UINT m_DefaultMaterialSlot = CDescriptorHeapAllocator::kInvalidSlot;  // null block for null-material groups

    Canvas::CGlyphCache m_GlyphCache;
    Gem::TGemPtr<Canvas::XGfxSurface> m_pGlyphAtlasSurface;
};
