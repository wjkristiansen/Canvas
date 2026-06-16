//================================================================================================
// MeshData12
//================================================================================================

#pragma once

#include <vector>
#include "CanvasGfx12.h"
#include "DescriptorHeapAllocator.h"

class CDevice12;

//------------------------------------------------------------------------------------------------
class CMeshData12 : public TGfxElement<Canvas::XGfxMeshData>
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxMeshData)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    // Per-material-group GPU resources. Empty pBuffer entries mean the stream
    // was not provided at creation time. UV0/Tangent buffers are optional.
    struct GroupResources
    {
        Canvas::GfxResourceAllocation       PositionVB;
        Canvas::GfxResourceAllocation       NormalVB;
        Canvas::GfxResourceAllocation       UV0VB;          // empty when absent
        Canvas::GfxResourceAllocation       TangentVB;      // empty when absent
        Canvas::GfxResourceAllocation       BoneWeightsVB;  // empty when not skinned
        Canvas::GfxResourceAllocation       BoneIndicesVB;  // empty when not skinned
        Gem::TGemPtr<Canvas::XGfxMaterial>  pMaterial;      // may be null

        // Base slot of this group's persistent mesh-stream descriptor block in the device's shared
        // heap (kMeshStreamDescriptorCount SRVs; CDevice12::CreateMeshData defines the
        // slot-to-stream mapping).  kInvalidSlot until populated by CDevice12::CreateMeshData.
        UINT                                StreamDescriptorBase = CDescriptorHeapAllocator::kInvalidSlot;
    };

    // pDevice (the owning device, which holds the shared descriptor heap) is required and never
    // changes; the mesh frees its per-group persistent descriptor blocks back to it on
    // destruction.
    CMeshData12(Canvas::XCanvas* pCanvas, CDevice12* pDevice, PCSTR name = nullptr);
    ~CMeshData12();

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    // Base slot of the given group's persistent mesh-stream descriptor block, or
    // CDescriptorHeapAllocator::kInvalidSlot if it has none.
    UINT GetStreamDescriptorBase(uint32_t materialIndex) const
    {
        return materialIndex < m_Groups.size() ? m_Groups[materialIndex].StreamDescriptorBase
                                               : CDescriptorHeapAllocator::kInvalidSlot;
    }

    // XGfxMeshData interface
    GEMMETHOD_(uint32_t, GetNumMaterialGroups)() override;
    GEMMETHOD_(Canvas::GfxResourceAllocation*, GetVertexBuffer)(uint32_t materialIndex, Canvas::GfxVertexBufferType type) override;
    GEMMETHOD_(Canvas::XGfxMaterial*, GetMaterial)(uint32_t materialIndex) override;
    GEMMETHOD_(Canvas::GfxPrimitiveTopology, GetTopology)() override { return m_Topology; }
    GEMMETHOD_(uint32_t, GetTotalVertexCount)() override { return m_TotalVertexCount; }
    GEMMETHOD_(Canvas::Math::AABB, GetLocalBounds)() override;

    // Internal: replace the group table wholesale (used by CDevice12::CreateMeshData).
    void SetGroups(std::vector<GroupResources> &&groups) { m_Groups = std::move(groups); }
    void SetTopology(Canvas::GfxPrimitiveTopology t) { m_Topology = t; }
    void SetTotalVertexCount(uint32_t n) { m_TotalVertexCount = n; }
    void SetLocalBounds(const Canvas::Math::AABB &b) { m_LocalBounds = b; }

private:
    std::vector<GroupResources>    m_Groups;
    Canvas::GfxPrimitiveTopology   m_Topology         = Canvas::GfxPrimitiveTopology::TriangleList;
    uint32_t                       m_TotalVertexCount = 0;
    Canvas::Math::AABB             m_LocalBounds;  // empty by default
    CDevice12*                     m_pDevice          = nullptr;  // owns the shared descriptor heap
};

