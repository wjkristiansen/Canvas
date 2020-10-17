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
    m_pContext(pContext)
{
}

//------------------------------------------------------------------------------------------------
Result CSwapChainVk::Initialize(HWND hWnd, bool Windowed)
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

        // Create fence
        VkFenceCreateInfo vkFenceCreateInfo = {};
        vkFenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        ThrowVkFailure(vkCreateFence(vkDevice, &vkFenceCreateInfo, nullptr, &m_VkFence));

        // Create the swapchain
        RECT rcWnd;
        GetClientRect(hWnd, &rcWnd);
        VkSwapchainCreateInfoKHR swapchainCreateInfo = {};
        swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCreateInfo.pNext = nullptr;
        swapchainCreateInfo.surface = vkSurface;
        swapchainCreateInfo.minImageCount = 2;
        swapchainCreateInfo.imageFormat = VkFormat::VK_FORMAT_R16G16B16A16_SFLOAT;
        swapchainCreateInfo.imageColorSpace = VkColorSpaceKHR::VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        swapchainCreateInfo.imageExtent.height = rcWnd.bottom - rcWnd.top;
        swapchainCreateInfo.imageExtent.width = rcWnd.right - rcWnd.left;
        swapchainCreateInfo.imageArrayLayers = 1;
        swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainCreateInfo.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        swapchainCreateInfo.clipped = VK_TRUE;

        VkSwapchainKHR vkTempSwapChain;
        ThrowVkFailure(vkCreateSwapchainKHR(vkDevice, &swapchainCreateInfo, nullptr, &vkTempSwapChain));
        vkSwapChain.Attach(vkTempSwapChain, vkDevice);

        uint32_t ImageCount;
        ThrowVkFailure(vkGetSwapchainImagesKHR(vkDevice, vkSwapChain.Get(), &ImageCount, nullptr));
        Images.resize(ImageCount);
        ThrowVkFailure(vkGetSwapchainImagesKHR(vkDevice, vkSwapChain.Get(), &ImageCount, Images.data()));

        m_VkImages = std::move(Images);
        m_VkSwapChain.Swap(vkSwapChain);

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
        // Transition to Presentation Layout...
        VkImageMemoryBarrier ImageBarriers[1] = {};
        ImageBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ImageBarriers[0].srcAccessMask = 0;
        ImageBarriers[0].dstAccessMask = 0;
        ImageBarriers[0].image = m_VkImages[m_ImageIndex];
        ImageBarriers[0].srcQueueFamilyIndex = m_pContext->m_QueueFamilyIndex;
        ImageBarriers[0].dstQueueFamilyIndex = m_pContext->m_QueueFamilyIndex;
        ImageBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ImageBarriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        ImageBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;
        ImageBarriers[0].subresourceRange.baseArrayLayer = 0;
        ImageBarriers[0].subresourceRange.baseMipLevel = 0;
        ImageBarriers[0].subresourceRange.layerCount = 1;
        ImageBarriers[0].subresourceRange.levelCount = 1;
        vkCmdPipelineBarrier(
            vkCmdBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
            VK_DEPENDENCY_BY_REGION_BIT,
            0,
            nullptr,
            0,
            nullptr,
            1,
            ImageBarriers
        );

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
            m_VkFence,
        };

        ThrowVkFailure(vkWaitForFences(vkDevice, 1, Fences, VK_TRUE, UINT64_MAX));
    }
    catch (const VkError &e)
    {
        return e.GemResult();
    }

    return Result::Success;
}
