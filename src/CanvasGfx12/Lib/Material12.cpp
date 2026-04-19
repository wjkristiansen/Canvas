//================================================================================================
// Material12
//================================================================================================

#include "pch.h"
#include "Material12.h"

//------------------------------------------------------------------------------------------------
CMaterial12::CMaterial12(Canvas::XCanvas *pCanvas, PCSTR name) :
    TGfxElement(pCanvas)
{
    if (name != nullptr)
        SetName(name);
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
