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

    // Bucketed pool for UPLOAD heap buffers (host-write staging).
    // Buckets cover powers of 2 from 256 B to 4 MB.  Oversized requests
    // get a dedicated unpooled buffer.
    static constexpr uint64_t kMinBucketSize = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT; // 256 bytes
    static constexpr uint64_t kMaxBucketSize = 4 * 1024 * 1024;  // 4 MB
    static constexpr uint32_t kMinBucketLog2 = 8;   // log2(256)
    static constexpr uint32_t kMaxBucketLog2 = 22;  // log2(4 MB)
    static constexpr uint32_t kNumBuckets = kMaxBucketLog2 - kMinBucketLog2 + 1;  // 15
    static constexpr uint32_t kBucketPoolCap = 8;    // Max buffers retained per bucket

    std::vector<Gem::TGemPtr<Canvas::XGfxBuffer>> m_HostWriteBuckets[kNumBuckets];

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxDevice)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CDevice12(Canvas::XCanvas* pCanvas, PCSTR name = nullptr);
    ~CDevice12() = default;  // TGfxElement destructor handles Unregister

    Gem::Result Initialize();
    void Uninitialize() {}

    // XGfxDevice methods
    GEMMETHOD(CreateRenderQueue)(Canvas::XGfxRenderQueue **ppRenderQueue) final;
    GEMMETHOD(CreateMaterial)() final;
    GEMMETHOD(CreateSurface)(const Canvas::GfxSurfaceDesc &desc, Canvas::XGfxSurface **ppSurface) final;
    GEMMETHOD(CreateBuffer)(uint64_t sizeInBytes, Canvas::GfxMemoryUsage memoryUsage, Canvas::XGfxBuffer **ppBuffer) final;
    GEMMETHOD(AllocateHostWriteRegion)(uint64_t sizeInBytes, Canvas::GfxBufferSuballocation &suballocationInfo) final;
    GEMMETHOD_(void, FreeHostWriteRegion)(Canvas::GfxBufferSuballocation &suballocationInfo) final;
    GEMMETHOD(CreateMeshData)(
        uint32_t vertexCount,
        const Canvas::Math::FloatVector4 *positions,
        const Canvas::Math::FloatVector4 *normals,
        Canvas::XGfxRenderQueue *pRenderQueue,
        Canvas::XGfxMeshData **ppMesh) final;
    GEMMETHOD(CreateDebugMeshData)(
        uint32_t vertexCount,
        const Canvas::Math::FloatVector4 *positions,
        const Canvas::Math::FloatVector4 *normals,
        Canvas::XGfxRenderQueue *pRenderQueue,
        Canvas::XGfxMeshData **ppMesh) final;

    ID3D12Device10 *GetD3DDevice() const { return m_pD3DDevice; }

    Canvas::XLogger *GetLogger()
    {
        auto pCanvas = GetCanvas();
        return pCanvas ? pCanvas->GetLogger() : nullptr;
    }

    // UI vertex buffer paged allocation
    // Each pool has a buddy allocator (logical vertex-index space) + page table of GPU buffers
    struct UIVertexPool
    {
        std::unique_ptr<TBuddySuballocator<uint32_t>> pAllocator;
        std::vector<Gem::TGemPtr<Canvas::XGfxBuffer>> Pages;
        uint32_t PageCapacity = 0;      // Vertices per page (power of 2)
        uint64_t VertexStride = 0;      // Bytes per vertex
    };

    UIVertexPool m_UITextVertexPool;
    UIVertexPool m_UIRectVertexPool;

    void EnsureUIVertexPool(UIVertexPool& pool, uint32_t pageCapacity, uint64_t vertexStride);
    void GrowUIVertexPool(UIVertexPool& pool);

    // UI vertex buffer paged allocation (XGfxDevice interface — alloc + upload)
    GEMMETHOD(AllocUITextVertices)(uint32_t vertexCount, const void* pVertexData, Canvas::XGfxRenderQueue* pRQ, Canvas::GfxBufferSuballocation& out) final;
    GEMMETHOD_(void, FreeUITextVertices)(const Canvas::GfxBufferSuballocation& suballoc) final;
    GEMMETHOD(AllocUIRectVertices)(uint32_t vertexCount, const void* pVertexData, Canvas::XGfxRenderQueue* pRQ, Canvas::GfxBufferSuballocation& out) final;
    GEMMETHOD_(void, FreeUIRectVertices)(const Canvas::GfxBufferSuballocation& suballoc) final;
};
