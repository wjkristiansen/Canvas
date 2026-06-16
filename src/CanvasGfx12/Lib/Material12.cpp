//================================================================================================
// Material12
//================================================================================================

#include "pch.h"
#include "Material12.h"
#include "Device12.h"
#include "Surface12.h"

//------------------------------------------------------------------------------------------------
CMaterial12::CMaterial12(Canvas::XCanvas *pCanvas, CDevice12 *pDevice, PCSTR name) :
    TGfxElement(pCanvas),
    m_pDevice(pDevice)
{
    if (name != nullptr)
        SetName(name);

    // Allocate this material's persistent texture block (kMaterialDescriptorCount SRVs, one per
    // SupportedRole; see PopulateDescriptors) once and seed it from the current textures (all null
    // at creation; SetTexture refreshes it later).
    m_DescriptorBase = m_pDevice->AllocatePersistentDescriptors(CDevice12::kMaterialDescriptorCount);
    if (m_DescriptorBase == CDescriptorHeapAllocator::kInvalidSlot)
        throw Gem::GemError(Gem::Result::OutOfMemory);
    PopulateDescriptors();
}

//------------------------------------------------------------------------------------------------
CMaterial12::~CMaterial12()
{
    m_pDevice->FreePersistentDescriptors(m_DescriptorBase, CDevice12::kMaterialDescriptorCount);
}

//------------------------------------------------------------------------------------------------
void CMaterial12::PopulateDescriptors()
{
    // Material table slot order is albedo, normal, emissive, roughness, metallic, AO -- which
    // maps onto m_Textures (indexed by SupportedRole) in this order.
    const SupportedRole blockOrder[CDevice12::kMaterialDescriptorCount] = {
        Role_Albedo, Role_Normal, Role_Emissive, Role_Roughness, Role_Metallic, Role_AmbientOcclusion,
    };

    for (UINT i = 0; i < CDevice12::kMaterialDescriptorCount; ++i)
    {
        CSurface12* pSurf = nullptr;
        if (Canvas::XGfxSurface* pSurface = m_Textures[blockOrder[i]].Get())
        {
            Gem::TGemPtr<CSurface12> pSurf12;
            pSurface->QueryInterface(&pSurf12);
            pSurf = pSurf12.Get();
        }
        m_pDevice->WriteTexture2DSRV(m_DescriptorBase + i, pSurf);
    }
}

//------------------------------------------------------------------------------------------------
D3D12_GPU_DESCRIPTOR_HANDLE CMaterial12::GetTextureTableHandle()
{
    return m_pDevice->GetSrvGpuHandle(m_DescriptorBase);
}

//------------------------------------------------------------------------------------------------
bool CMaterial12::MapRole(Canvas::MaterialLayerRole role, SupportedRole &out)
{
    switch (role)
    {
    case Canvas::MaterialLayerRole::Albedo:
        out = Role_Albedo;
        return true;
    case Canvas::MaterialLayerRole::Normal:
        out = Role_Normal;
        return true;
    case Canvas::MaterialLayerRole::Roughness:
        out = Role_Roughness;
        return true;
    case Canvas::MaterialLayerRole::Metallic:
        out = Role_Metallic;
        return true;
    case Canvas::MaterialLayerRole::AmbientOcclusion:
        out = Role_AmbientOcclusion;
        return true;
    case Canvas::MaterialLayerRole::Emissive:
        out = Role_Emissive;
        return true;
    default:
        return false;
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CMaterial12::SetTexture(Canvas::MaterialLayerRole role, Canvas::XGfxSurface *pSurface)
{
    SupportedRole mapped;
    if (!MapRole(role, mapped))
        return Gem::Result::NotImplemented;

    m_Textures[mapped] = pSurface;

    // Refresh the persistent descriptor block so the new texture is visible to draws.
    // NOTE: this writes the shared, shader-visible descriptor in place; configure materials
    // before they are used for rendering, since a change while a frame referencing this block
    // is in flight is not synchronized against the GPU.
    PopulateDescriptors();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(Canvas::XGfxSurface *) CMaterial12::GetTexture(Canvas::MaterialLayerRole role)
{
    SupportedRole mapped;
    if (!MapRole(role, mapped))
        return nullptr;

    return m_Textures[mapped].Get();
}
