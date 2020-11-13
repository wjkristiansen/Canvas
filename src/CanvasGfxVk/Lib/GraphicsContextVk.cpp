//================================================================================================
// GraphicsContextVk
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
CCommandBufferManager::~CCommandBufferManager()
{
    if (m_VkCommandPool.Get() != VK_NULL_HANDLE)
    {
        VkDevice vkDevice = m_VkCommandPool.Device();
        for (auto &entry : m_CommandBuffers)
        {
            vkFreeCommandBuffers(vkDevice, m_VkCommandPool.Get(), 1, &entry.vkCommandBuffer);
        }
    }
}

//------------------------------------------------------------------------------------------------
Gem::Result CCommandBufferManager::Initialize(VkDevice vkDevice, uint32_t NumBuffers, uint32_t QueueFamilyIndex)
{
    try
    {
        UniqueVkCommandPool vkCommandPool;

        // Create command pool
        CVkCommandPoolCreateInfo poolCreateInfo(VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, QueueFamilyIndex);
        VkCommandPool vkTempCommandPool;
        ThrowVkFailure(vkCreateCommandPool(vkDevice, &poolCreateInfo, nullptr, &vkTempCommandPool));
        vkCommandPool.Attach(vkTempCommandPool, vkDevice);

        // Allocate the command buffers
        std::vector<VkCommandBuffer> commandBuffers(NumBuffers);

        CVkCommandBufferAllocateInfo AllocateInfo(vkCommandPool.Get(), VK_COMMAND_BUFFER_LEVEL_PRIMARY, NumBuffers);
        ThrowVkFailure(vkAllocateCommandBuffers(vkDevice, &AllocateInfo, commandBuffers.data()));

        for (VkCommandBuffer commandBuffer : commandBuffers)
        {
            m_CommandBuffers.emplace_back(commandBuffer);
        }

        m_QueueFamilyIndex = QueueFamilyIndex;

        m_VkCommandPool.Swap(vkCommandPool);
    }
    catch (const VkError &e)
    {
        return e.GemResult();
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
// Acquires the next vkCommandBuffer in the rotation
VkCommandBuffer CCommandBufferManager::AcquireCommandBuffer() // throw(Gem::GemError)
{
    if (0 == m_CommandBuffers.size())
        return VK_NULL_HANDLE;

    std::shared_ptr<CFenceVk> pFence = m_CommandBuffers.front().pFence;
    if (pFence && VK_NULL_HANDLE != pFence->GetVkFence())
    {
        VkFence fences[] = { pFence->GetVkFence() };
        // Wait for the fence
        vkWaitForFences(m_VkCommandPool.Device(), 1, fences, VK_TRUE, UINT_MAX);
    }

    VkCommandBuffer commandBuffer = m_CommandBuffers.front().vkCommandBuffer;
    m_CommandBuffers.pop_front();

    try
    {
        CVkCommandBufferBeginInfo beginInfo(VkCommandBufferUsageFlagBits::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        ThrowVkFailure(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    }
    catch (const VkError &e)
    {
        throw Gem::GemError(e.GemResult());
    }

    return commandBuffer;
}

//------------------------------------------------------------------------------------------------
// Unacquires a vkCommandBuffer and blocks Acquire until vkFence is signalled
void CCommandBufferManager::UnacquireCommandBuffer(VkCommandBuffer vkCommandBuffer, const std::shared_ptr<CFenceVk> &pFence) // throw(std::bad_alloc)
{
    m_CommandBuffers.emplace_back(vkCommandBuffer, pFence);
}

//------------------------------------------------------------------------------------------------
CGraphicsContextVk::CGraphicsContextVk(CDeviceVk *pVkDevice) :
    m_pDevice(pVkDevice),
    m_VkCommandBuffer(VK_NULL_HANDLE),
    m_VkQueue(VK_NULL_HANDLE)
{
}

//------------------------------------------------------------------------------------------------
Result CGraphicsContextVk::Initialize()
{
    VkQueue vkQueue = VK_NULL_HANDLE;
    CInstanceVk *pInstance = CInstanceVk::GetSingleton();
    CFunctionSentinel Sentinel(pInstance->Logger(), "CGraphicsContextVk::Initialize");

    try
    {
        VkDeviceQueueCreateInfo &deviceQueueCreateInfo = m_pDevice->GetDeviceQueueCreateInfo(CDeviceVk::QueueFamily::Graphics);

        ThrowGemError(CCommandBufferManager::Initialize(GetVkDevice(), 16, deviceQueueCreateInfo.queueFamilyIndex));

        // Get the queue
        vkGetDeviceQueue(GetVkDevice(), deviceQueueCreateInfo.queueFamilyIndex, 0, &m_VkQueue);

        m_QueueFamilyIndex = deviceQueueCreateInfo.queueFamilyIndex;
        m_VkCommandBuffer = AcquireCommandBuffer();
    }
    catch (const VkError &e)
    {
        Sentinel.SetResultCode(e.GemResult());
        return e.GemResult();
    }
    catch (const GemError &e)
    {
        Sentinel.SetResultCode(e.Result());
        return e.Result();
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
// XGfxGraphicsContext methods
GEMMETHODIMP CGraphicsContextVk::CreateSwapChain(HWND hWnd, bool Windowed, Canvas::XGfxSwapChain **ppSwapChain, Canvas::GfxFormat Format, UINT NumBuffers)
{
    CInstanceVk *pInstance = CInstanceVk::GetSingleton();
    CFunctionSentinel Sentinel(pInstance->Logger(), "CGraphicsContextVk::CreateSwapChain");
    VkFormat vkFormat = CanvasFormatToVkFormat(Format);

    try
    {
        CSwapChainVk *pSwapChain = new TGeneric<CSwapChainVk>(this); // throw std::bad_alloc()
        ThrowGemError(pSwapChain->Initialize(hWnd, Windowed, vkFormat));
        ThrowGemError(pSwapChain->QueryInterface(ppSwapChain));
    }
    catch (std::bad_alloc &)
    {
        Sentinel.SetResultCode(Result::OutOfMemory);
        return Result::OutOfMemory;
    }
    catch (const GemError &e)
    {
        Sentinel.SetResultCode(e.Result());
        return e.Result();
    }
    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CGraphicsContextVk::CopyBuffer(Canvas::XGfxBuffer *pDest, Canvas::XGfxBuffer *pSource)
{
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CGraphicsContextVk::ClearSurface(Canvas::XGfxSurface *pSurface, const float Color[4])
{
}

//------------------------------------------------------------------------------------------------
Gem::Result CGraphicsContextVk::FlushImpl()
{
    try
    {
        //ThrowFailedHResult(m_pCommandList->Close());

        //ID3D12CommandList *exlist[] =
        //{
        //	m_pCommandList
        //};

        //m_pCommandQueue->ExecuteCommandLists(1, exlist);

        //m_pCommandQueue->Signal(m_pFence, ++m_FenceValue);

        return Result::Success;
    }
    catch (Gem::Result &e)
    {
        return e;
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CGraphicsContextVk::Flush()
{
    std::unique_lock<std::mutex> Lock(m_mutex);
    CInstanceVk *pInstance = CInstanceVk::GetSingleton();
    CFunctionSentinel Sentinel(pInstance->Logger(), "CGraphicsContextVk::Flush", QLog::Category::Debug);
    try
    {
        ThrowGemError(FlushImpl());

        //ThrowFailedHResult(m_pCommandList->Reset(m_pCommandAllocator, nullptr));
    }
    catch (Gem::Result &e)
    {
        return e;
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CGraphicsContextVk::FlushAndPresent(Canvas::XGfxSwapChain *pSwapChain)
{
    std::unique_lock<std::mutex> Lock(m_mutex);
    CInstanceVk *pInstance = CInstanceVk::GetSingleton();
    CFunctionSentinel Sentinel(pInstance->Logger(), "CGraphicsContextVk::FlushAndPresent", QLog::Category::Debug);

    try
    {
        CSwapChainVk *pIntSwapChain = reinterpret_cast<CSwapChainVk *>(pSwapChain);
        //pIntSwapChain->m_pSurface->SetDesiredResourceState(m_pDevice->m_ResourceStateManager, D3D12_RESOURCE_STATE_COMMON);
        //ApplyResourceBarriers();
        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.layerCount = 1;
        subresourceRange.levelCount = 1;
        CVkImageMemoryBarrier ImageMemoryBarriers[1] =
        {
            {
                VkAccessFlagBits::VK_ACCESS_MEMORY_READ_BIT,
                VkAccessFlagBits::VK_ACCESS_MEMORY_READ_BIT,
                VkImageLayout::VK_IMAGE_LAYOUT_GENERAL,
                VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                m_QueueFamilyIndex,
                m_QueueFamilyIndex,
                pIntSwapChain->m_VkImages[pIntSwapChain->m_ImageIndex],
                subresourceRange
            }
        };
        vkCmdPipelineBarrier(
            m_VkCommandBuffer, 
            VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VkPipelineStageFlagBits::VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VkDependencyFlagBits::VK_DEPENDENCY_BY_REGION_BIT,
            0,
            nullptr,
            0,
            nullptr,
            1,
            ImageMemoryBarriers);
        

        // Submit the command buffer
        vkEndCommandBuffer(m_VkCommandBuffer);

        VkCommandBuffer commandBuffers[1] =
        {
            m_VkCommandBuffer
        };

        VkSubmitInfo submitInfo[1] =
        {
            CVkSubmitInfo(1, commandBuffers)
        };
        ThrowVkFailure(vkQueueSubmit(m_VkQueue, 1, submitInfo, VK_NULL_HANDLE));

        //ThrowGemError(FlushImpl());

        //ThrowGemError(pIntSwapChain->Present());

        //m_pCommandQueue->Signal(m_pFence, ++m_FenceValue);

        //// Rotate command allocators
        //m_pCommandAllocator = m_CommandAllocatorPool.RotateAllocators(this);
        //m_pCommandAllocator->Reset();

        //ThrowFailedHResult(m_pCommandList->Reset(m_pCommandAllocator, nullptr));
    }
    catch (const VkError &e)
    {
        Sentinel.SetResultCode(e.GemResult());
        return e.GemResult();
    }
    catch (const GemError &e)
    {
        Sentinel.SetResultCode(e.Result());
        return e.Result();
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CGraphicsContextVk::Wait()
{
    return Result::NotImplemented;
}

