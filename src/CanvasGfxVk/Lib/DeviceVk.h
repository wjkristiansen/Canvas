//================================================================================================
// DeviceVk
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CDeviceVk :
    public Canvas::XGfxDevice,
    public Gem::CGenericBase
{
public:
    CUniqueVkDevice m_VkDevice;
    enum class QueueFamily : uint32_t
    {
        Graphics,
        Copy
    };
    static const unsigned NumRequiredQueueFamilies = 2;
    VkDeviceQueueCreateInfo m_deviceQueueCreateInfo[NumRequiredQueueFamilies];

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxDevice)
    END_GEM_INTERFACE_MAP()

    CDeviceVk();
    ~CDeviceVk();

    // XGfxDevice methods
    GEMMETHOD(Present)() final;
    GEMMETHOD(CreateGraphicsContext)(Canvas::XGfxGraphicsContext **ppGraphicsContext) final;

    // Internal methods
    Gem::Result Initialize();
    VkDeviceQueueCreateInfo &GetDeviceQueueCreateInfo(QueueFamily Family) { return m_deviceQueueCreateInfo[uint32_t(Family)]; }
};
