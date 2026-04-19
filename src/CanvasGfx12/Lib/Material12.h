//================================================================================================
// Material12 - XGfxMaterial implementation for the D3D12 backend.
//
// Phase 3 scope: pure CPU-side property bag (textures + factors). The
// RenderQueue does not yet read these into GPU descriptor tables — that
// plumbing is paired with the HLSL changes in Phase 4.
//================================================================================================

#pragma once

#include "CanvasGfx12.h"

class CMaterial12 : public TGfxElement<Canvas::XGfxMaterial>
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxMaterial)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CMaterial12(Canvas::XCanvas *pCanvas, PCSTR name = nullptr);

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    // XGfxMaterial
    GEMMETHOD(SetTexture)(Canvas::MaterialLayerRole role, Canvas::XGfxSurface *pSurface) final;
    GEMMETHOD_(Canvas::XGfxSurface *, GetTexture)(Canvas::MaterialLayerRole role) final;

    GEMMETHOD_(void, SetBaseColorFactor)(const Canvas::Math::FloatVector4 &factor) final
    {
        m_BaseColorFactor = factor;
    }
    GEMMETHOD_(Canvas::Math::FloatVector4, GetBaseColorFactor)() final
    {
        return m_BaseColorFactor;
    }

    GEMMETHOD_(void, SetEmissiveFactor)(const Canvas::Math::FloatVector4 &factor) final
    {
        m_EmissiveFactor = factor;
    }
    GEMMETHOD_(Canvas::Math::FloatVector4, GetEmissiveFactor)() final
    {
        return m_EmissiveFactor;
    }

private:
    // Maps MaterialLayerRole values onto the Phase 1 PBR layer set we care
    // about. Roles outside this set are accepted by SetTexture but not
    // currently consumed by the backend.
    enum SupportedRole : uint32_t
    {
        Role_Albedo   = 0,
        Role_Normal   = 1,
        Role_Emissive = 2,
        Role_Count    = 3,
    };

    static bool MapRole(Canvas::MaterialLayerRole role, SupportedRole &out);

    Gem::TGemPtr<Canvas::XGfxSurface> m_Textures[Role_Count];
    Canvas::Math::FloatVector4 m_BaseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Canvas::Math::FloatVector4 m_EmissiveFactor  = { 0.0f, 0.0f, 0.0f, 0.0f };
};
