//================================================================================================
// MeshData12
//================================================================================================

#pragma once

#include <vector>
#include "CanvasGfx12.h"

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
        Canvas::GfxResourceAllocation       UV0VB;       // empty when absent
        Canvas::GfxResourceAllocation       TangentVB;   // empty when absent
        Gem::TGemPtr<Canvas::XGfxMaterial>  pMaterial;   // may be null
    };

    CMeshData12(Canvas::XCanvas* pCanvas, PCSTR name = nullptr);

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    // XGfxMeshData interface
    GEMMETHOD_(uint32_t, GetNumMaterialGroups)() override;
    GEMMETHOD_(Canvas::GfxResourceAllocation*, GetVertexBuffer)(uint32_t materialIndex, Canvas::GfxVertexBufferType type) override;
    GEMMETHOD_(Canvas::XGfxMaterial*, GetMaterial)(uint32_t materialIndex) override;
    GEMMETHOD_(Canvas::GfxPrimitiveTopology, GetTopology)() override { return m_Topology; }
    GEMMETHOD_(uint32_t, GetTotalVertexCount)() override { return m_TotalVertexCount; }

    // Internal: replace the group table wholesale (used by CDevice12::CreateMeshData).
    void SetGroups(std::vector<GroupResources> &&groups) { m_Groups = std::move(groups); }
    void SetTopology(Canvas::GfxPrimitiveTopology t) { m_Topology = t; }
    void SetTotalVertexCount(uint32_t n) { m_TotalVertexCount = n; }

private:
    std::vector<GroupResources>    m_Groups;
    Canvas::GfxPrimitiveTopology   m_Topology         = Canvas::GfxPrimitiveTopology::TriangleList;
    uint32_t                       m_TotalVertexCount = 0;
};

