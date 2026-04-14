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

    // Upload ring buffer for host-write staging.
    // A single committed UPLOAD buffer with a write pointer that advances linearly.
    // Oversized allocations (>50% of ring) get a dedicated committed resource.
    Gem::TGemPtr<Canvas::XGfxBuffer> m_pUploadRingBuffer;
    uint64_t m_UploadRingSize = 1 * 1024 * 1024;           // 1 MB
    uint64_t m_UploadRingWriteOffset = 0;                   // Next write position
    uint64_t m_UploadRingReadOffset = 0;                    // Oldest unreclaimable position

    struct UploadRingFrameMarker
    {
        UINT64 FenceValue;
        uint64_t WriteOffset;   // Write offset at the time this frame was submitted
    };
    std::deque<UploadRingFrameMarker> m_UploadRingFrameMarkers;

    void MarkUploadRingFrameEnd(UINT64 fenceValue);
    void ReclaimUploadRingSpace(UINT64 completedFenceValue);
    void GrowUploadRingBuffer(uint64_t newSize);

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
    GEMMETHOD(AllocateHostWriteRegion)(uint64_t sizeInBytes, Canvas::GfxResourceAllocation &suballocationInfo) final;
    GEMMETHOD_(void, FreeHostWriteRegion)(Canvas::GfxResourceAllocation &suballocationInfo) final;
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

    // Resource allocator (placed + committed)
    CResourceAllocator m_ResourceAllocator;

    // Vertex buffer suballocation (XGfxDevice interface — alloc + upload)
    GEMMETHOD(AllocVertexBuffer)(uint32_t vertexCount, uint32_t vertexStride, const void* pVertexData, Canvas::XGfxRenderQueue* pRQ, Canvas::GfxResourceAllocation& out) final;
    GEMMETHOD_(void, FreeVertexBuffer)(const Canvas::GfxResourceAllocation& suballoc) final;

    // Texture upload (XGfxDevice interface — delegates to RQ for GPU copy)
    GEMMETHOD(UploadTextureRegion)(
        Canvas::XGfxSurface *pDstSurface,
        uint32_t dstX, uint32_t dstY,
        uint32_t width, uint32_t height,
        const void *pData,
        uint32_t srcRowPitch,
        Canvas::XGfxRenderQueue *pRenderQueue) final;
};
