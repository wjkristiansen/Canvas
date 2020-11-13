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
    VkPhysicalDevice m_VkPhysicalDevice = VK_NULL_HANDLE;
    enum class QueueFamily : uint32_t
    {
        Graphics,
        Copy
    };
    static const unsigned NumRequiredQueueFamilies = 2;
    CVkDeviceQueueCreateInfo m_deviceQueueCreateInfo[NumRequiredQueueFamilies];

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxDevice)
    END_GEM_INTERFACE_MAP()

    CDeviceVk() = default;
    ~CDeviceVk();

    // XGfxDevice methods
    GEMMETHOD(Present)() final;
    GEMMETHOD(CreateGraphicsContext)(Canvas::XGfxGraphicsContext **ppGraphicsContext) final;

    // Internal methods
    Gem::Result Initialize();
    VkDeviceQueueCreateInfo &GetDeviceQueueCreateInfo(QueueFamily Family) { return m_deviceQueueCreateInfo[uint32_t(Family)]; }
};
