//================================================================================================
// SwapChainVk
//================================================================================================

#include "pch.h"

#include "CanvasGfxVk.h"
#include "InstanceVk.h"
#include "DeviceVk.h"
#include "GraphicsContextVk.h"
#include "SwapChainVk.h"

using namespace Canvas;
using namespace Gem;

//------------------------------------------------------------------------------------------------
CSwapChainVk::CSwapChainVk(CGraphicsContextVk *pContext) :
    m_pContext(pContext),
    m_ColorSpace(VkColorSpaceKHR::VK_COLORSPACE_SRGB_NONLINEAR_KHR),
    m_SwapchainFormat(VkFormat::VK_FORMAT_UNDEFINED),
    m_ViewFormat(VkFormat::VK_FORMAT_UNDEFINED)
{
}

//------------------------------------------------------------------------------------------------
Result CSwapChainVk::Initialize(HWND hWnd, bool Windowed, VkFormat ViewFormat)
{
    UniqueVkSwapchainKHR vkSwapChain;
    CInstanceVk *pInstance = CInstanceVk::GetSingleton();
    CDeviceVk *pDevice = m_pContext->m_pDevice;
    VkDevice vkDevice = pDevice->m_VkDevice.Get();
    VkInstance vkInstance = pInstance->m_VkInstance;
    CFunctionSentinel Sentinel(pInstance->Logger(), "CSwapChainVk::Initialize");
    std::vector<VkImage> Images;

    try
    {
        // Create the win32 surface from the given HWND
        VkWin32SurfaceCreateInfoKHR win32SurfaceCreateInfo = {};

        win32SurfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        win32SurfaceCreateInfo.pNext = nullptr;
        win32SurfaceCreateInfo.hwnd = hWnd;
        win32SurfaceCreateInfo.hinstance = GetModuleHandle(NULL);

        VkSurfaceKHR vkSurface;
        ThrowVkFailure(vkCreateWin32SurfaceKHR(vkInstance, &win32SurfaceCreateInfo, nullptr, &vkSurface));

        // Make sure the surface is supported for presentation
        VkBool32 isSurfaceSupported;
        ThrowVkFailure(vkGetPhysicalDeviceSurfaceSupportKHR(pDevice->m_VkPhysicalDevice, m_pContext->m_QueueFamilyIndex, vkSurface, &isSurfaceSupported));

        if (!isSurfaceSupported)
        {
            return Gem::Result::Unavailable;
        }

        // Create fence
        VkFenceCreateInfo vkFenceCreateInfo = {};
        vkFenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence vkFence;
        ThrowVkFailure(vkCreateFence(vkDevice, &vkFenceCreateInfo, nullptr, &vkFence));
        m_VkFence.Attach(vkFence, vkDevice, nullptr);

        // Get the surface formats
        uint32_t formatCount;
        VkPhysicalDeviceSurfaceInfo2KHR deviceSurfaceInfo{};
        deviceSurfaceInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
        deviceSurfaceInfo.surface = vkSurface;
        ThrowVkFailure(vkGetPhysicalDeviceSurfaceFormats2KHR(pDevice->m_VkPhysicalDevice, &deviceSurfaceInfo, &formatCount, nullptr));
        std::vector<VkSurfaceFormat2KHR> surfaceFormats(formatCount);
        for (auto &surfaceFormat : surfaceFormats) { surfaceFormat.sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR; }
        ThrowVkFailure(vkGetPhysicalDeviceSurfaceFormats2KHR(pDevice->m_VkPhysicalDevice, &deviceSurfaceInfo, &formatCount, surfaceFormats.data()));

        // For now, select the first enumerated format
        const uint32_t formatIndex = 0;

        // Get the available present modes
        uint32_t presentModeCount;
        ThrowVkFailure(vkGetPhysicalDeviceSurfacePresentModesKHR(pDevice->m_VkPhysicalDevice, vkSurface, &presentModeCount, nullptr));
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        ThrowVkFailure(vkGetPhysicalDeviceSurfacePresentModesKHR(pDevice->m_VkPhysicalDevice, vkSurface, &presentModeCount, presentModes.data()));

        // For now, select the first present mode
        const uint32_t presentModeIndex = 0;

        std::vector<VkFormat> Formats;
        for (const auto &surfaceFormat : surfaceFormats) { Formats.push_back(surfaceFormat.surfaceFormat.format); }
        Formats.push_back(ViewFormat);
        VkColorSpaceKHR ColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

        // Create the swapchain
        VkImageFormatListCreateInfo imageFormatListCreateInfo = {};
        imageFormatListCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
        imageFormatListCreateInfo.viewFormatCount = static_cast<uint32_t>(Formats.size());
        imageFormatListCreateInfo.pViewFormats = Formats.data();

        RECT rcWnd;
        GetClientRect(hWnd, &rcWnd);
        VkSwapchainCreateInfoKHR swapchainCreateInfo = {};
        swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCreateInfo.pNext = &imageFormatListCreateInfo;
        swapchainCreateInfo.surface = vkSurface;
        swapchainCreateInfo.minImageCount = 3;
        swapchainCreateInfo.imageFormat = Formats[0];
        swapchainCreateInfo.imageColorSpace = ColorSpace;
        swapchainCreateInfo.imageExtent.height = rcWnd.bottom - rcWnd.top;
        swapchainCreateInfo.imageExtent.width = rcWnd.right - rcWnd.left;
        swapchainCreateInfo.imageArrayLayers = 1;
        swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainCreateInfo.presentMode = presentModes[presentModeIndex];
        swapchainCreateInfo.clipped = VK_TRUE;
        swapchainCreateInfo.flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;

        VkSwapchainKHR vkTempSwapChain;
        ThrowVkFailure(vkCreateSwapchainKHR(vkDevice, &swapchainCreateInfo, nullptr, &vkTempSwapChain));
        vkSwapChain.Attach(vkTempSwapChain, vkDevice);

        uint32_t ImageCount;
        ThrowVkFailure(vkGetSwapchainImagesKHR(vkDevice, vkSwapChain.Get(), &ImageCount, nullptr));
        Images.resize(ImageCount);
        ThrowVkFailure(vkGetSwapchainImagesKHR(vkDevice, vkSwapChain.Get(), &ImageCount, Images.data()));

        m_VkImages = std::move(Images);
        m_VkSwapChain.Swap(vkSwapChain);

        m_SwapchainFormat = Formats[0];
        m_ViewFormat = ViewFormat;
        m_ColorSpace = ColorSpace;
    }
    catch (const VkError &e)
    {
        Sentinel.SetResultCode(e.GemResult());
        return e.GemResult();
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CSwapChainVk::Present()
{
    try
    {
        std::unique_lock<std::mutex> Lock(m_mutex);
        CDeviceVk *pDevice = m_pContext->m_pDevice;
        VkDevice vkDevice = pDevice->m_VkDevice.Get();
        VkQueue vkQueue = m_pContext->m_VkQueue;
        VkCommandBuffer vkCmdBuffer = m_pContext->m_VkCommandBuffer;
/*
        // Queue the Present
        VkResult vkResult;
        VkPresentInfoKHR PresentInfo = {};
        PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        PresentInfo.waitSemaphoreCount = 0;
        PresentInfo.swapchainCount = 1;
        PresentInfo.pSwapchains = &m_VkSwapChain;
        PresentInfo.pImageIndices = &m_ImageIndex;
        PresentInfo.pResults = &vkResult;
        ThrowVkFailure(vkQueuePresentKHR(m_pContext->m_VkQueue, &PresentInfo));
        ThrowVkFailure(vkResult);

        // Get the new back buffer
        ThrowVkFailure(vkAcquireNextImageKHR(
            vkDevice,
            m_VkSwapChain.Get(), 
            UINT64_MAX, 
            VK_NULL_HANDLE, 
            m_VkFence,
            &m_ImageIndex));

        m_pSurface->Rename(m_VkImages[m_ImageIndex]);
 */
    }
    catch (const VkError &e)
    {
        return e.GemResult();
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSwapChainVk::GetSurface(XGfxSurface **ppSurface)
{
    *ppSurface = nullptr;
    std::unique_lock<std::mutex> Lock(m_mutex);
    return m_pSurface ? m_pSurface->QueryInterface(ppSurface) : Result::NotFound;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSwapChainVk::WaitForLastPresent()
{
    std::unique_lock<std::mutex> Lock(m_mutex);

    try
    {
        CDeviceVk *pDevice = m_pContext->m_pDevice;
        VkDevice vkDevice = pDevice->m_VkDevice.Get();

        // Wait for the swapchain fence
        VkFence Fences[] =
        {
            m_VkFence.Get(),
        };

        ThrowVkFailure(vkWaitForFences(vkDevice, 1, Fences, VK_TRUE, UINT64_MAX));
    }
    catch (const VkError &e)
    {
        return e.GemResult();
    }

    return Result::Success;
}
