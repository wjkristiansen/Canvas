//================================================================================================
// SurfaceVk
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CSurfaceVk :
    public Canvas::XGfxSurface,
    public Gem::CGenericBase
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxSurface)
    END_GEM_INTERFACE_MAP()

    VkImage m_VkImage;

    CSurfaceVk(VkImage VkImage) :
        m_VkImage(VkImage) {}

    void Rename(VkImage VkImage) { m_VkImage = VkImage; }
};

