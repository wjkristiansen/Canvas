//================================================================================================
// Material12 - XGfxMaterial implementation for the D3D12 backend.
//
// Phase 3 scope: pure CPU-side property bag (textures + factors). The
// RenderQueue does not yet read these into GPU descriptor tables — that
// plumbing is paired with the HLSL changes in Phase 4.
//================================================================================================

#pragma once

#include "CanvasGfx12.h"
#include "DescriptorHeapAllocator.h"

class CDevice12;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif

class CMaterial12 : public TGfxElement<Canvas::XGfxMaterial>
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxMaterial)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    // pDevice (the owning device, which holds the shared descriptor heap) is required and never
    // changes.  The ctor allocates + populates this material's persistent texture descriptor
    // block (kMaterialDescriptorCount SRVs, one per SupportedRole) and throws
    // Gem::Result::OutOfMemory if the persistent region is exhausted.
    CMaterial12(Canvas::XCanvas *pCanvas, CDevice12 *pDevice, PCSTR name = nullptr);
    ~CMaterial12();

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    // GPU handle of the material's persistent texture table, bound by DrawMesh.
    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureTableHandle();

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

    GEMMETHOD_(void, SetRoughMetalAOFactor)(const Canvas::Math::FloatVector4 &factor) final
    {
        m_RoughMetalAOFactor = factor;
    }
    GEMMETHOD_(Canvas::Math::FloatVector4, GetRoughMetalAOFactor)() final
    {
        return m_RoughMetalAOFactor;
    }

    GEMMETHOD_(void, SetDisplacement)(const Canvas::GfxDisplacementDesc *pDesc) final
    {
        if (pDesc)
        {
            m_HasDisplacement = true;
            m_Displacement    = *pDesc;
            // Hold a strong ref on the displacement-map surface so the
            // material keeps it alive for as long as displacement is
            // configured.
            m_pDisplacementMap = pDesc->pDisplacementMap;
        }
        else
        {
            m_HasDisplacement = false;
            m_Displacement    = {};
            m_pDisplacementMap = nullptr;
        }
    }
    GEMMETHOD_(const Canvas::GfxDisplacementDesc *, GetDisplacement)() const final
    {
        return m_HasDisplacement ? &m_Displacement : nullptr;
    }

private:
    // Maps MaterialLayerRole values onto our PBR layer set.
    enum SupportedRole : uint32_t
    {
        Role_Albedo           = 0,
        Role_Normal           = 1,
        Role_Roughness        = 2,
        Role_Metallic         = 3,
        Role_AmbientOcclusion = 4,
        Role_Emissive         = 5,
        Role_Count            = 6,
    };

    static bool MapRole(Canvas::MaterialLayerRole role, SupportedRole &out);

    // (Re)write this material's persistent texture SRVs from m_Textures, in material-table slot
    // order (defined by the blockOrder table in PopulateDescriptors).
    void PopulateDescriptors();

    CDevice12* m_pDevice = nullptr;  // owns the shared descriptor heap
    UINT       m_DescriptorBase = CDescriptorHeapAllocator::kInvalidSlot;  // base of the persistent texture block (kMaterialDescriptorCount SRVs)

    Gem::TGemPtr<Canvas::XGfxSurface> m_Textures[Role_Count];
    Canvas::Math::FloatVector4 m_BaseColorFactor    = { 1.0f, 1.0f, 1.0f, 1.0f };
    Canvas::Math::FloatVector4 m_EmissiveFactor     = { 0.0f, 0.0f, 0.0f, 0.0f };
    Canvas::Math::FloatVector4 m_RoughMetalAOFactor = { 1.0f, 0.0f, 1.0f, 0.0f };

    // Displacement extension storage.  m_Displacement.pDisplacementMap is
    // a raw observer pointer (XGfxSurface*); m_pDisplacementMap is the
    // matching strong ref that keeps the surface alive.  Both are
    // populated together by SetDisplacement.
    bool                              m_HasDisplacement = false;
    Canvas::GfxDisplacementDesc       m_Displacement;
    Gem::TGemPtr<Canvas::XGfxSurface> m_pDisplacementMap;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
