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

// Subresource layout state tracking for Enhanced Barriers
struct SubresourceLayoutState {
    // If uniformLayout is set, all subresources share this layout (common case, memory efficient)
    // If not set, perSubresourceLayouts contains individual subresource states
    std::optional<D3D12_BARRIER_LAYOUT> uniformLayout;
    std::unordered_map<UINT, D3D12_BARRIER_LAYOUT> perSubresourceLayouts;
    
    // Get layout for a specific subresource (or 0xFFFFFFFF for all)
    D3D12_BARRIER_LAYOUT GetLayout(UINT subresource) const
    {
        if (uniformLayout.has_value())
        {
            return uniformLayout.value();
        }
        auto it = perSubresourceLayouts.find(subresource);
        return (it != perSubresourceLayouts.end()) ? it->second : D3D12_BARRIER_LAYOUT_COMMON;
    }
    
    // Set layout for specific subresource(s)
    void SetLayout(UINT subresource, D3D12_BARRIER_LAYOUT layout)
    {
        if (subresource == 0xFFFFFFFF)
        {
            // Setting all subresources - collapse to uniform layout
            uniformLayout = layout;
            perSubresourceLayouts.clear();
        }
        else
        {
            // Setting individual subresource
            if (uniformLayout.has_value())
            {
                // Split uniform layout into per-subresource tracking
                // Note: We don't know total subresource count here, so we'll track on-demand
                uniformLayout.reset();
            }
            perSubresourceLayouts[subresource] = layout;
        }
    }
    
    // Check if all tracked subresources have the same layout (can collapse to uniform)
    bool CanCollapseToUniform(D3D12_BARRIER_LAYOUT& outLayout) const
    {
        if (uniformLayout.has_value())
        {
            outLayout = uniformLayout.value();
            return true;
        }
        if (perSubresourceLayouts.empty())
        {
            outLayout = D3D12_BARRIER_LAYOUT_COMMON;
            return true;
        }
        // Check if all per-subresource layouts match
        D3D12_BARRIER_LAYOUT firstLayout = perSubresourceLayouts.begin()->second;
        for (const auto& [sub, layout] : perSubresourceLayouts)
        {
            if (layout != firstLayout) return false;
        }
        outLayout = firstLayout;
        return true;
    }
};

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
    CResourceStateManager m_ResourceStateManager;
    uint64_t m_HostWriteSize = 4 * 1024 * 1024; // 4 MB default
    TBuddySuballocator<uint64_t> m_HostWriteSuballocator;
    Gem::TGemPtr<Canvas::XGfxBuffer> m_pHostWriteBuffer;
    
    // Committed layout tracking (actual GPU state after command list execution)
    // This is the single source of truth for resource layouts across all render queues
    std::unordered_map<ID3D12Resource*, SubresourceLayoutState> m_TextureCurrentLayouts;

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
    GEMMETHOD(AllocateHostWriteRegion)(uint64_t sizeInBytes, Canvas::GfxSuballocation &suballocationInfo) final;
    GEMMETHOD_(void, FreeHostWriteRegion)(Canvas::GfxSuballocation &suballocationInfo) final;
    GEMMETHOD(CreateDebugMeshData)(
        uint32_t vertexCount,
        const Canvas::Math::FloatVector4 *positions,
        const Canvas::Math::FloatVector4 *normals,
        Canvas::XGfxRenderQueue *pRenderQueue,
        Canvas::XGfxMeshData **ppMesh) final;

    ID3D12Device10 *GetD3DDevice() const { return m_pD3DDevice; }
    
    // Initialize committed layout state for a texture (called when resources are created)
    void InitializeTextureLayout(ID3D12Resource* pResource, D3D12_BARRIER_LAYOUT initialLayout);

    Canvas::XLogger *GetLogger()
    {
        auto pCanvas = GetCanvas();
        return pCanvas ? pCanvas->GetLogger() : nullptr;
    }
};
