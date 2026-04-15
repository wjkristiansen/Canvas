//================================================================================================
// MeshData12
//================================================================================================

#pragma once

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

    CMeshData12(Canvas::XCanvas* pCanvas, PCSTR name = nullptr);

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    // XGfxMeshData interface
    GEMMETHOD_(uint32_t, GetNumMaterialGroups)() override;
    GEMMETHOD_(Canvas::GfxResourceAllocation*, GetVertexBuffer)(uint32_t materialIndex, Canvas::GfxVertexBufferType type) override;
    GEMMETHOD_(Canvas::XGfxMaterial*, GetMaterial)(uint32_t materialIndex) override;

    // Internal methods for setting up buffers
    void SetPositionBuffer(const Canvas::GfxResourceAllocation& vb);
    void SetNormalBuffer(const Canvas::GfxResourceAllocation& vb);

private:
    Canvas::GfxResourceAllocation m_PositionVB;
    Canvas::GfxResourceAllocation m_NormalVB;
};
