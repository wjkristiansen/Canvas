//================================================================================================
// GraphicsDeviceVk
//================================================================================================

#include "pch.h"
#include "CanvasGfxVk.h"
#include "InstanceVk.h"
#include "DeviceVk.h"
#include "GraphicsContextVk.h"

using namespace Canvas;
using namespace Gem;

#define VK_GET_DEVICE_PROC_ADDR(func, device ) \
    func = (PFN_##func) vkGetDeviceProcAddr(device, #func);

//------------------------------------------------------------------------------------------------
bool CheckPhysicalDeviceSupportsExtensions(VkPhysicalDevice physicalDevice, const char *const *extensionNames, uint32_t numExtensionNames)
{
    try
    {
        // Check extension support
        uint32_t numExtensionProperties;
        ThrowVkFailure(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &numExtensionProperties, nullptr));
        std::vector<VkExtensionProperties> availableExtensions(numExtensionProperties);
        ThrowVkFailure(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &numExtensionProperties, availableExtensions.data()));

        uint32_t numSupportedExtensions = 0;
        for (uint32_t i = 0; i < numExtensionNames; ++i)
        {
            const char *extensionName = extensionNames[i];
            bool isSupported = false;
            for (auto &extProp : availableExtensions)
            {
                if (0 == std::strcmp(extensionName, extProp.extensionName))
                {
                    isSupported = true;
                    break;
                }
            }

            if (!isSupported)
            {
                continue;
            }

            numSupportedExtensions++;
        }

        return numSupportedExtensions == numExtensionNames;
    }
    catch (const VkError &)
    {
        return false;
    }
}

//------------------------------------------------------------------------------------------------
struct QueueRequirement
{
    uint32_t requiredFlags;
    uint32_t excludeFlags;
};

//------------------------------------------------------------------------------------------------
uint32_t CheckPhysicalDeviceSupportsQueues(
    VkPhysicalDevice physicalDevice, 
    const QueueRequirement *queueRequirements,
    uint32_t numRequiredQueues, 
    VkDeviceQueueCreateInfo *deviceQueueCreateInfo)
{
    // Check queue family support
    uint32_t numSupportedQueueFamilies;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &numSupportedQueueFamilies, nullptr);
    assert(numSupportedQueueFamilies > 0);
    std::vector<VkQueueFamilyProperties> supportedQueueFamilies(numSupportedQueueFamilies);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &numSupportedQueueFamilies, supportedQueueFamilies.data());
    uint32_t numMatches = 0;
    for (uint32_t j = 0; j < numRequiredQueues; ++j)
    {
        for (uint32_t i = 0; i < numSupportedQueueFamilies; ++i)
        {
            if (supportedQueueFamilies[i].queueFlags & queueRequirements[j].excludeFlags)
            {
                continue;
            }

            if(queueRequirements[j].requiredFlags == (supportedQueueFamilies[i].queueFlags & queueRequirements[j].requiredFlags))
            {
                deviceQueueCreateInfo[j].queueFamilyIndex = i;
                deviceQueueCreateInfo[j].queueCount = 1;
                deviceQueueCreateInfo[j].flags = 0;
                numMatches++;
                break;
            }
        }

        if (numMatches == numRequiredQueues)
        {
            // Done searching
            break;
        }
    }

    return numMatches;
}

//------------------------------------------------------------------------------------------------
Result CDeviceVk::Initialize()
{
    CUniqueVkDevice vkDevice;
    CInstanceVk *pInstance = CInstanceVk::GetSingleton();
    CFunctionSentinel Sentinel(pInstance->Logger(), "CDeviceVk::Initialize");

    try
    {
        // Enumerate available vulkan devices
        uint32_t numPhysicalDevices;
        ThrowVkFailure(vkEnumeratePhysicalDevices(pInstance->m_VkInstance, &numPhysicalDevices, nullptr));
        std::vector<VkPhysicalDevice> physicalDevices(numPhysicalDevices);
        ThrowVkFailure(vkEnumeratePhysicalDevices(pInstance->m_VkInstance, &numPhysicalDevices, physicalDevices.data()));

        uint32_t deviceIndex = uint32_t(-1);

        // Enumerate required device extensions
        static const char *requiredDeviceExtensions[] =
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
            VK_KHR_MAINTENANCE2_EXTENSION_NAME,
            VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME,
        };
        static const uint32_t numRequiredDeviceExtensions = _countof(requiredDeviceExtensions);

        // Identify required queue families
        static const QueueRequirement requiredQueues[2] =
        {
            // QueueFamily::Graphics
            {
                VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT,
                0,
            },
            // QueueFamily::Copy
            {
                VK_QUEUE_TRANSFER_BIT,
                VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT
            }
        };
        static const uint32_t numRequiredQueueFamilies = _countof(requiredQueues);

        static const float StandardQueuePriority[] = { 1.0 };

        // Initialize m_deviceQueueCreateInfo
        for (unsigned i = 0; i < NumRequiredQueueFamilies; ++i)
        {
            m_deviceQueueCreateInfo[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            m_deviceQueueCreateInfo[i].pNext = nullptr;
            m_deviceQueueCreateInfo[i].pQueuePriorities = StandardQueuePriority;
            m_deviceQueueCreateInfo[i].queueCount = 0;
            m_deviceQueueCreateInfo[i].flags = 0;
        }

        // Find device that supports all required features (e.g. extensions, queue families, etc)
        uint32_t matchingQueueFamilyCount = 0;
        for (uint32_t i = 0; i < numPhysicalDevices; ++i)
        {
            if (!CheckPhysicalDeviceSupportsExtensions(physicalDevices[i], requiredDeviceExtensions, numRequiredDeviceExtensions))
            {
                continue;
            }

            // Check queue family support
            matchingQueueFamilyCount = CheckPhysicalDeviceSupportsQueues(
                physicalDevices[i],
                requiredQueues,
                numRequiredQueueFamilies,
                m_deviceQueueCreateInfo);
            if (numRequiredQueueFamilies != matchingQueueFamilyCount)
            {
                continue;
            }

            deviceIndex = i;
            break;
        }

        if (matchingQueueFamilyCount < numRequiredQueueFamilies)
        {
            throw(VkError(VkResult::VK_ERROR_LAYER_NOT_PRESENT));
        }

        // Create the default device
        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pNext = nullptr;
        deviceCreateInfo.queueCreateInfoCount = numRequiredQueueFamilies;
        deviceCreateInfo.pQueueCreateInfos = m_deviceQueueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = numRequiredDeviceExtensions;
        deviceCreateInfo.ppEnabledExtensionNames = requiredDeviceExtensions;
        ThrowVkFailure(vkCreateDevice(physicalDevices[deviceIndex], &deviceCreateInfo, nullptr, &vkDevice));

        // Initialize global device functions
        FOR_EACH_VK_DEVICE_EXTENSION_FUNC(VK_GET_DEVICE_PROC_ADDR, vkDevice.Get());

        m_VkDevice.Swap(vkDevice);
        m_VkPhysicalDevice = physicalDevices[deviceIndex];

        return Result::Success;
    }
    catch (const VkError &e)
    {
        Gem::Result result = e.GemResult();
        Sentinel.SetResultCode(result);
        return result;
    }
}

CDeviceVk::~CDeviceVk()
{
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDeviceVk::CreateGraphicsContext(XGfxGraphicsContext **ppContext)
{
    CFunctionSentinel Sentinel(CInstanceVk::GetSingleton()->Logger(), "CDeviceVk::CreateGraphicsContext");

    *ppContext = nullptr;

    try
    {
        TGemPtr<CGraphicsContextVk> pGraphicsContext = new TGeneric<CGraphicsContextVk>(this); // throw(bad_alloc), throw Gem::GemError
        ThrowGemError(pGraphicsContext->Initialize());
        ThrowGemError(pGraphicsContext->QueryInterface(ppContext));
    }
    catch (GemError &e)
    {
        Sentinel.SetResultCode(e.Result());
        return e.Result();
    }
    catch (std::bad_alloc &)
    {
        Sentinel.SetResultCode(Result::OutOfMemory);
        return Result::OutOfMemory;
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CDeviceVk::Present()
{
    return Result::NotImplemented;
}
