//================================================================================================
// InstanceVk
//================================================================================================

#include "pch.h"
#include "CanvasGfxVk.h"
#include "InstanceVk.h"
#include "DeviceVk.h"

using namespace Gem;
using namespace Canvas;

//------------------------------------------------------------------------------------------------
// Define vulkan globals
FOR_EACH_VK_INSTANCE_EXTENSION_FUNC(DEFINE_VK_GLOBAL_FUNC)
FOR_EACH_VK_NULL_INSTANCE_FUNC(DEFINE_VK_GLOBAL_FUNC)
FOR_EACH_VK_FUNC(DEFINE_VK_GLOBAL_FUNC)

//------------------------------------------------------------------------------------------------
#define VK_GET_INSTANCE_PROC_ADDR(func, instance) \
    func = (PFN_##func) vkGetInstanceProcAddr(instance, #func);

//------------------------------------------------------------------------------------------------
CInstanceVk *CInstanceVk::m_pThis = nullptr;

//------------------------------------------------------------------------------------------------
CInstanceVk::CInstanceVk(QLog::CLogClient * pLogClient) :
    m_Logger(pLogClient, "CANVAS GRAPHICS VK")
{
    assert(m_pThis == nullptr);

    m_pThis = this;
}

//------------------------------------------------------------------------------------------------
static const std::vector<const char *> s_validationLayer
{
    {"VK_LAYER_KHRONOS_validation"}
};

//------------------------------------------------------------------------------------------------
static bool CheckValidationLayersSupported()
{
    uint32_t layerCount;

    try
    {
        ThrowVkFailure(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));
        std::vector<VkLayerProperties> layers(layerCount);
        ThrowVkFailure(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()));

        for(const auto &layerProp : layers)
        {
            if(0 == strcmp(s_validationLayer[0], layerProp.layerName))
            {
                return true;
            }
        }
    }
    catch(const VkError& )
    {
        return false;
    }

    return false;
}

//------------------------------------------------------------------------------------------------
VKAPI_ATTR VkBool32 VKAPI_CALL CInstanceVk::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData
)
{
    if(messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        m_pThis->Logger().LogError(pCallbackData->pMessage);
    }
    return VK_FALSE;
}

//------------------------------------------------------------------------------------------------
Result CInstanceVk::Initialize()
{
    CFunctionSentinel Sentinel(m_Logger, "CInstanceVk::Initialize");

    VkInstance vkInstance = VK_NULL_HANDLE;

    try
    {
        // Load the vulkan loader
        wil::unique_hmodule VkModule(LoadLibraryA("vulkan-1.dll"));
        if (VkModule.get() == NULL)
        {
            // Vulkan dll not found
            throw GemError(Result::Unavailable);
        }

        vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)GetProcAddress(VkModule.get(), "vkGetDeviceProcAddr");
        if (nullptr == vkGetDeviceProcAddr)
        {
            throw GemError(Result::Unavailable);
        }

        vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(VkModule.get(), "vkGetInstanceProcAddr");
        if (nullptr == vkGetInstanceProcAddr)
        {
            throw GemError(Result::Unavailable);
        }

        // Init the null-instance Vulkan functions
        FOR_EACH_VK_NULL_INSTANCE_FUNC(VK_GET_INSTANCE_PROC_ADDR, VK_NULL_HANDLE);

        // Enumerate required interface extensions
        std::vector<const char *> requiredInstanceExtensions
        {
            { VK_KHR_WIN32_SURFACE_EXTENSION_NAME },
            { VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME },
            { VK_KHR_SURFACE_EXTENSION_NAME },
        };

        if(IsValidateLayersEnabled() && CheckValidationLayersSupported())
        {
            requiredInstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        // Create the Vulkan instance
        static const uint32_t numRequireInstanceExtensions = static_cast<uint32_t>(requiredInstanceExtensions.size());
        CVkInstanceCreateInfo vkCreateInfo(
            0, 
            nullptr, 
            0, 
            nullptr, 
            numRequireInstanceExtensions, 
            requiredInstanceExtensions.data());

        if(IsValidateLayersEnabled() && CheckValidationLayersSupported())
        {
            vkCreateInfo.enabledLayerCount = 1;
            vkCreateInfo.ppEnabledLayerNames = s_validationLayer.data();
        }

        ThrowVkFailure(vkCreateInstance(&vkCreateInfo, nullptr, &vkInstance));

        CVkDebugUtilsMessengerCreateInfoEXT dbgCreateInfo(
            0,
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | 
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            DebugCallback,
            nullptr); 

        // Init the remaining Vulkan instance functions
        FOR_EACH_VK_FUNC(VK_GET_INSTANCE_PROC_ADDR, vkInstance);
        FOR_EACH_VK_INSTANCE_EXTENSION_FUNC(VK_GET_INSTANCE_PROC_ADDR, vkInstance);

        // Setup the debug message callback
        ThrowVkFailure(vkCreateDebugUtilsMessengerEXT(vkInstance, &dbgCreateInfo, nullptr, &m_vkMessenger));

        m_VkModule = std::move(VkModule);
        m_VkInstance = vkInstance;
    }
    catch(const VkError &e)
    {
        Sentinel.SetResultCode(e.GemResult());
        if (vkInstance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(vkInstance, nullptr);
        }

        return e.GemResult();
    }

    return Result::Success;
}

CInstanceVk::~CInstanceVk()
{
    if (m_VkInstance != VK_NULL_HANDLE)
    {
        if(m_vkMessenger != VK_NULL_HANDLE)
        {
            vkDestroyDebugUtilsMessengerEXT(m_VkInstance, m_vkMessenger, nullptr);
        }

        vkDestroyInstance(m_VkInstance, nullptr);
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CInstanceVk::CreateGfxDevice(XGfxDevice **ppDevice)
{
    CFunctionSentinel Sentinel(m_Logger, "CInstanceVk::CreateGfxDevice");

    *ppDevice = nullptr;

    try
    {
        TGemPtr<CDeviceVk> pGraphicsDevice = new TGeneric<CDeviceVk>(); // throw(bad_alloc), throw Gem::GemError
        ThrowGemError(pGraphicsDevice->Initialize());
        ThrowGemError(pGraphicsDevice->QueryInterface(ppDevice));
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
