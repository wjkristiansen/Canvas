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
    GEMMETHOD(CreateMaterial)() final;
    GEMMETHOD(CreateSurface)(const Canvas::GfxSurfaceDesc &desc, Canvas::XGfxSurface **ppSurface) final;
    GEMMETHOD(CreateBuffer)(uint64_t sizeInBytes, Canvas::GfxMemoryUsage memoryUsage, Canvas::XGfxBuffer **ppBuffer) final;
    GEMMETHOD(CreateMeshData)(
        uint32_t vertexCount,
        const Canvas::Math::FloatVector4 *positions,
        const Canvas::Math::FloatVector4 *normals,
        Canvas::XGfxRenderQueue *pRenderQueue,
        Canvas::XGfxMeshData **ppMesh,
        const char* name = nullptr) final;
    GEMMETHOD(CreateDebugMeshData)(
        uint32_t vertexCount,
        const Canvas::Math::FloatVector4 *positions,
        const Canvas::Math::FloatVector4 *normals,
        Canvas::XGfxRenderQueue *pRenderQueue,
        Canvas::XGfxMeshData **ppMesh,
        const char* name = nullptr) final;

    ID3D12Device10 *GetD3DDevice() const { return m_pD3DDevice; }

    Canvas::XLogger *GetLogger()
    {
        auto pCanvas = GetCanvas();
        return pCanvas ? pCanvas->GetLogger() : nullptr;
    }

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
    GEMMETHOD(AllocVertexBuffer)(uint32_t vertexCount, uint32_t vertexStride, const void* pVertexData, Canvas::XGfxRenderQueue* pRQ, Canvas::GfxResourceAllocation& inOut) final;

    // Texture upload (XGfxDevice interface — delegates to RQ for GPU copy)
    GEMMETHOD(UploadTextureRegion)(
        Canvas::XGfxSurface *pDstSurface,
        uint32_t dstX, uint32_t dstY,
        uint32_t width, uint32_t height,
        const void *pData,
        uint32_t srcRowPitch,
        Canvas::XGfxRenderQueue *pRenderQueue) final;

    GEMMETHOD(CreateTextElement)(Canvas::XUITextElement **ppElement) final;
    GEMMETHOD(CreateRectElement)(Canvas::XUIRectElement **ppElement) final;

    Canvas::XGfxSurface* GetGlyphAtlasSurface();

private:
    Gem::TGemPtr<Canvas::XGfxSurface> m_pGlyphAtlasSurface;
};
