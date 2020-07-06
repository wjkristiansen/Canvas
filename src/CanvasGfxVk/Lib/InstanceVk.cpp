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

        // Create the Vulkan instance
        VkInstanceCreateInfo vkCreateInfo = {};

        // Enumerate required interface extensions
        static const char *requiredInstanceExtensions[] =
        {
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
            VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
            VK_KHR_SURFACE_EXTENSION_NAME,
        };

        static const uint32_t numRequireInstanceExtensions = _countof(requiredInstanceExtensions);
        vkCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        vkCreateInfo.pNext = nullptr;
        vkCreateInfo.flags = 0;
        vkCreateInfo.pApplicationInfo = nullptr;
        vkCreateInfo.enabledExtensionCount = numRequireInstanceExtensions;
        vkCreateInfo.ppEnabledExtensionNames = requiredInstanceExtensions;

        ThrowVkFailure(vkCreateInstance(&vkCreateInfo, nullptr, &vkInstance));

        // Init the remaining Vulkan instance functions
        FOR_EACH_VK_FUNC(VK_GET_INSTANCE_PROC_ADDR, vkInstance);
        FOR_EACH_VK_INSTANCE_EXTENSION_FUNC(VK_GET_INSTANCE_PROC_ADDR, vkInstance);

        m_VkModule = std::move(VkModule);
        m_VkInstance = vkInstance;
    }
    catch(const VkError &e)
    {
        Gem::Result result(VkToGemResult(e.Result()));
        Sentinel.SetResultCode(result);
        if (vkInstance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(vkInstance, nullptr);
        }

        return result;
    }

    return Result::Success;
}

CInstanceVk::~CInstanceVk()
{
    if (m_VkInstance)
    {
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
