//================================================================================================
// CanvasGfxVk
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
// Global functions
extern VkFormat CanvasFormatToVkFormat(Canvas::GfxFormat Fmt);

//------------------------------------------------------------------------------------------------
#define FOR_EACH_VK_NULL_INSTANCE_FUNC(macro, ...) \
    macro(vkCreateInstance, ##__VA_ARGS__) \
    macro(vkEnumerateInstanceLayerProperties, ##__VA_ARGS__) \
    macro(vkEnumerateInstanceExtensionProperties, ##__VA_ARGS__) \

#define FOR_EACH_VK_INSTANCE_FUNC(macro, ...) \
    macro(vkDestroyInstance, ##__VA_ARGS__) \
    macro(vkEnumeratePhysicalDevices, ##__VA_ARGS__) \

#define FOR_EACH_VK_PHYSICAL_DEVICE_FUNC(macro, ...) \
    macro(vkGetPhysicalDeviceFeatures, ##__VA_ARGS__) /* PhysicalDevice */ \
    macro(vkGetPhysicalDeviceFormatProperties, ##__VA_ARGS__) /* PhysicalDevice */ \
    macro(vkGetPhysicalDeviceProperties, ##__VA_ARGS__) /* PhysicalDevice */ \
    macro(vkGetPhysicalDeviceQueueFamilyProperties, ##__VA_ARGS__) /* PhysicalDevice */ \
    macro(vkGetPhysicalDeviceMemoryProperties, ##__VA_ARGS__) /* PhysicalDevice */ \
    macro(vkGetPhysicalDeviceImageFormatProperties, ##__VA_ARGS__) /* PhysicalDevice */ \
    macro(vkCreateDevice, ##__VA_ARGS__) /* PhysicalDevice */ \
    macro(vkEnumerateDeviceExtensionProperties, ##__VA_ARGS__) /* PhysicalDevice */ \
    macro(vkEnumerateDeviceLayerProperties, ##__VA_ARGS__) /* PhysicalDevice */ \
    macro(vkGetPhysicalDeviceSparseImageFormatProperties, ##__VA_ARGS__) /* PhysicalDevice */ \

#define FOR_EACH_VK_DEVICE_FUNC(macro, ...) \
    macro(vkDestroyDevice, ##__VA_ARGS__) /* Device */ \
    macro(vkGetDeviceQueue, ##__VA_ARGS__) /* Device */ \
    macro(vkDeviceWaitIdle, ##__VA_ARGS__) /* Device */ \
    macro(vkAllocateMemory, ##__VA_ARGS__) /* Device */ \
    macro(vkFreeMemory, ##__VA_ARGS__) /* Device */ \
    macro(vkMapMemory, ##__VA_ARGS__) /* Device */ \
    macro(vkUnmapMemory, ##__VA_ARGS__) /* Device */ \
    macro(vkFlushMappedMemoryRanges, ##__VA_ARGS__) /* Device */ \
    macro(vkInvalidateMappedMemoryRanges, ##__VA_ARGS__) /* Device */ \
    macro(vkGetDeviceMemoryCommitment, ##__VA_ARGS__) /* Device */ \
    macro(vkBindBufferMemory, ##__VA_ARGS__) /* Device */ \
    macro(vkBindImageMemory, ##__VA_ARGS__) /* Device */ \
    macro(vkGetBufferMemoryRequirements, ##__VA_ARGS__) /* Device */ \
    macro(vkGetImageMemoryRequirements, ##__VA_ARGS__) /* Device */ \
    macro(vkGetImageSparseMemoryRequirements, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateFence, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyFence, ##__VA_ARGS__) /* Device */ \
    macro(vkResetFences, ##__VA_ARGS__) /* Device */ \
    macro(vkGetFenceStatus, ##__VA_ARGS__) /* Device */ \
    macro(vkWaitForFences, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateSemaphore, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroySemaphore, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateEvent, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyEvent, ##__VA_ARGS__) /* Device */ \
    macro(vkGetEventStatus, ##__VA_ARGS__) /* Device */ \
    macro(vkSetEvent, ##__VA_ARGS__) /* Device */ \
    macro(vkResetEvent, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateQueryPool, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyQueryPool, ##__VA_ARGS__) /* Device */ \
    macro(vkGetQueryPoolResults, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateBuffer, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyBuffer, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateBufferView, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyBufferView, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateImage, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyImage, ##__VA_ARGS__) /* Device */ \
    macro(vkGetImageSubresourceLayout, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateImageView, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyImageView, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateShaderModule, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyShaderModule, ##__VA_ARGS__) /* Device */ \
    macro(vkCreatePipelineCache, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyPipelineCache, ##__VA_ARGS__) /* Device */ \
    macro(vkGetPipelineCacheData, ##__VA_ARGS__) /* Device */ \
    macro(vkMergePipelineCaches, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateGraphicsPipelines, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateComputePipelines, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyPipeline, ##__VA_ARGS__) /* Device */ \
    macro(vkCreatePipelineLayout, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyPipelineLayout, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateSampler, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroySampler, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateDescriptorSetLayout, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyDescriptorSetLayout, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateDescriptorPool, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyDescriptorPool, ##__VA_ARGS__) /* Device */ \
    macro(vkResetDescriptorPool, ##__VA_ARGS__) /* Device */ \
    macro(vkAllocateDescriptorSets, ##__VA_ARGS__) /* Device */ \
    macro(vkFreeDescriptorSets, ##__VA_ARGS__) /* Device */ \
    macro(vkUpdateDescriptorSets, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateFramebuffer, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyFramebuffer, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateRenderPass, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyRenderPass, ##__VA_ARGS__) /* Device */ \
    macro(vkGetRenderAreaGranularity, ##__VA_ARGS__) /* Device */ \
    macro(vkCreateCommandPool, ##__VA_ARGS__) /* Device */ \
    macro(vkDestroyCommandPool, ##__VA_ARGS__) /* Device */ \
    macro(vkResetCommandPool, ##__VA_ARGS__) /* Device */ \
    macro(vkAllocateCommandBuffers, ##__VA_ARGS__) /* Device */ \
    macro(vkFreeCommandBuffers, ##__VA_ARGS__) /* Device */ \

#define FOR_EACH_VK_QUEUE_FUNC(macro, ...) \
    macro(vkQueueSubmit, ##__VA_ARGS__) /* Queue */ \
    macro(vkQueueWaitIdle, ##__VA_ARGS__) /* Queue */ \
    macro(vkQueueBindSparse, ##__VA_ARGS__) /* Queue */ \

#define FOR_EACH_VK_COMMAND_BUFFER_FUNC(macro, ...) \
    macro(vkBeginCommandBuffer, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkEndCommandBuffer, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkResetCommandBuffer, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdBindPipeline, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdSetViewport, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdSetScissor, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdSetLineWidth, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdSetDepthBias, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdSetBlendConstants, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdSetDepthBounds, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdSetStencilCompareMask, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdSetStencilWriteMask, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdSetStencilReference, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdBindDescriptorSets, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdBindIndexBuffer, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdBindVertexBuffers, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdDraw, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdDrawIndexed, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdDrawIndirect, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdDrawIndexedIndirect, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdDispatch, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdDispatchIndirect, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdCopyBuffer, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdCopyImage, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdBlitImage, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdCopyBufferToImage, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdCopyImageToBuffer, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdUpdateBuffer, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdFillBuffer, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdClearColorImage, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdClearDepthStencilImage, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdClearAttachments, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdResolveImage, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdSetEvent, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdResetEvent, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdWaitEvents, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdPipelineBarrier, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdBeginQuery, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdEndQuery, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdResetQueryPool, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdWriteTimestamp, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdCopyQueryPoolResults, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdPushConstants, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdBeginRenderPass, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdNextSubpass, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdEndRenderPass, ##__VA_ARGS__) /* CommandBuffer*/ \
    macro(vkCmdExecuteCommands, ##__VA_ARGS__) /* CommandBuffer*/ \

#define FOR_EACH_VK_KHR_SURFACE_FUNC(macro, ...) \
    macro(vkDestroySurfaceKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkGetPhysicalDeviceSurfaceSupportKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkGetPhysicalDeviceSurfaceFormatsKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkGetPhysicalDeviceSurfacePresentModesKHR, ##__VA_ARGS__) /* SurfaceKHR */ \

#define FOR_EACH_VK_KHR_SWAPCHAIN_FUNC(macro, ...) \
    macro(vkCreateSwapchainKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkDestroySwapchainKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkGetSwapchainImagesKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkAcquireNextImageKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkQueuePresentKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkGetDeviceGroupPresentCapabilitiesKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkGetDeviceGroupSurfacePresentModesKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkGetPhysicalDevicePresentRectanglesKHR, ##__VA_ARGS__) /* SurfaceKHR */ \
    macro(vkAcquireNextImage2KHR, ##__VA_ARGS__) /* SurfaceKHR */ \

#define FOR_EACH_VK_KHR_WIN32_SURFACE_FUNC(macro, ...) \
    macro(vkCreateWin32SurfaceKHR, ##__VA_ARGS__) \
    macro(vkGetPhysicalDeviceWin32PresentationSupportKHR, ##__VA_ARGS__)

#define FOR_EACK_VK_KHR_SURFACE_FUNC(macro, ...) \
    macro(vkDestroySurfaceKHR, ##__VA_ARGS__) \
    macro(vkGetPhysicalDeviceSurfaceSupportKHR, ##__VA_ARGS__) \
    macro(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, ##__VA_ARGS__) \
    macro(vkGetPhysicalDeviceSurfaceFormatsKHR, ##__VA_ARGS__) \
    macro(vkGetPhysicalDeviceSurfacePresentModesKHR, ##__VA_ARGS__) \

#define FOR_EACH_VK_KHR_GET_SURFACE_CAPABILITIES_2_FUNC(macro, ...) \
    macro(vkGetPhysicalDeviceSurfaceCapabilities2KHR, ##__VA_ARGS__) \
    macro(vkGetPhysicalDeviceSurfaceFormats2KHR, ##__VA_ARGS__) \

#define FOR_EACH_VK_DEBUG_UTILS_FUNC(macro, ...) \
    macro(vkCreateDebugUtilsMessengerEXT, ##__VA_ARGS__) \
    macro(vkDestroyDebugUtilsMessengerEXT, ##__VA_ARGS__) \

#define FOR_EACH_VK_FUNC(macro, ...) \
    FOR_EACH_VK_INSTANCE_FUNC(macro, ##__VA_ARGS__) \
    FOR_EACH_VK_PHYSICAL_DEVICE_FUNC(macro, ##__VA_ARGS__) \
    FOR_EACH_VK_DEVICE_FUNC(macro, ##__VA_ARGS__) \
    FOR_EACH_VK_QUEUE_FUNC(macro, ##__VA_ARGS__) \
    FOR_EACH_VK_COMMAND_BUFFER_FUNC(macro, ##__VA_ARGS__) \

#define FOR_EACH_VK_INSTANCE_EXTENSION_FUNC(macro, ...) \
    FOR_EACH_VK_KHR_WIN32_SURFACE_FUNC(macro, ##__VA_ARGS__) \
    FOR_EACK_VK_KHR_SURFACE_FUNC(macro, ##__VA_ARGS__) \
    FOR_EACH_VK_KHR_GET_SURFACE_CAPABILITIES_2_FUNC(macro, ##__VA_ARGS__) \
    FOR_EACH_VK_DEBUG_UTILS_FUNC(macro, ##__VA_ARGS__) \

#define FOR_EACH_VK_DEVICE_EXTENSION_FUNC(macro, ...) \
    FOR_EACH_VK_KHR_SWAPCHAIN_FUNC(macro, ##__VA_ARGS__) \

//------------------------------------------------------------------------------------------------
// Declare vulkan globals
#define DECLARE_VK_GLOBAL_FUNC(func, ...) \
extern PFN_##func func;

FOR_EACH_VK_NULL_INSTANCE_FUNC(DECLARE_VK_GLOBAL_FUNC)
FOR_EACH_VK_FUNC(DECLARE_VK_GLOBAL_FUNC)
FOR_EACH_VK_INSTANCE_EXTENSION_FUNC(DECLARE_VK_GLOBAL_FUNC)
FOR_EACH_VK_DEVICE_EXTENSION_FUNC(DECLARE_VK_GLOBAL_FUNC)

extern PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

#define DEFINE_VK_GLOBAL_FUNC(func, ...) \
PFN_##func func = nullptr;

//------------------------------------------------------------------------------------------------
inline Gem::Result VkToGemResult(VkResult vkResult)
{
    switch (vkResult)
    {
    case VK_SUCCESS:
        return Gem::Result::Success;
    case VK_NOT_READY:
        return Gem::Result::Success;
    case VK_TIMEOUT:
        return Gem::Result::Success; // Result::Timeout;
    case VK_EVENT_SET:
        return Gem::Result::Success;
    case VK_EVENT_RESET:
        return Gem::Result::Success;
    case VK_INCOMPLETE:
        return Gem::Result::Success;
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return Gem::Result::OutOfMemory;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return Gem::Result::OutOfMemory;
    case VK_ERROR_INITIALIZATION_FAILED:
        return Gem::Result::Fail;
    case VK_ERROR_DEVICE_LOST:
        return Gem::Result::Fail;
    case VK_ERROR_MEMORY_MAP_FAILED:
        return Gem::Result::Fail;
    case VK_ERROR_LAYER_NOT_PRESENT:
        return Gem::Result::NotFound;
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return Gem::Result::NotFound;
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return Gem::Result::NotFound;
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return Gem::Result::NotFound;
    case VK_ERROR_TOO_MANY_OBJECTS:
        return Gem::Result::InvalidArg;
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return Gem::Result::NotImplemented;
    case VK_ERROR_FRAGMENTED_POOL:
        return Gem::Result::Fail;
    case VK_ERROR_UNKNOWN:
        return Gem::Result::Fail;
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        return Gem::Result::OutOfMemory;
    case VK_ERROR_INVALID_EXTERNAL_HANDLE:
        return Gem::Result::InvalidArg;
    case VK_ERROR_FRAGMENTATION:
        return Gem::Result::Fail;
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
        return Gem::Result::Fail;
    case VK_ERROR_SURFACE_LOST_KHR:
        return Gem::Result::Fail;
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return Gem::Result::Fail;
    case VK_SUBOPTIMAL_KHR:
        return Gem::Result::Fail;
    case VK_ERROR_OUT_OF_DATE_KHR:
        return Gem::Result::Fail;
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
        return Gem::Result::Fail;
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return Gem::Result::Fail;
    case VK_ERROR_INVALID_SHADER_NV:
        return Gem::Result::Fail;
    case VK_ERROR_INCOMPATIBLE_VERSION_KHR:
        return Gem::Result::Fail;
    case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
        return Gem::Result::Fail;
    case VK_ERROR_NOT_PERMITTED_EXT:
        return Gem::Result::Fail;
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
        return Gem::Result::Fail;
    case VK_THREAD_IDLE_KHR:
        return Gem::Result::Fail;
    case VK_THREAD_DONE_KHR:
        return Gem::Result::Fail;
    case VK_OPERATION_DEFERRED_KHR:
        return Gem::Result::Fail;
    case VK_OPERATION_NOT_DEFERRED_KHR:
        return Gem::Result::Fail;
    case VK_PIPELINE_COMPILE_REQUIRED_EXT:
        return Gem::Result::Fail;
    }

    return Gem::Result::Fail;
}

//------------------------------------------------------------------------------------------------
class VkError
{
    VkResult m_Result;

public:
    VkError(enum VkResult result) :
        m_Result(result) {}

    VkResult Result() const { return m_Result; }
    Gem::Result GemResult() const { return VkToGemResult(m_Result); }
};

//------------------------------------------------------------------------------------------------
inline void ThrowVkFailure(VkResult result)
{
    if (result < 0)
    {
        throw VkError(result);
    }
}

//------------------------------------------------------------------------------------------------
class CUniqueVkDevice
{
    VkDevice m_VkDevice = VK_NULL_HANDLE;
    const VkAllocationCallbacks *m_pAllocator = nullptr;

    void Destroy()
    {
        if (m_VkDevice != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_VkDevice, m_pAllocator);
            Detach();
        }
    }

public:
    CUniqueVkDevice() = default;
    CUniqueVkDevice(VkDevice vkDevice, const VkAllocationCallbacks *pAllocator = nullptr) :
        m_VkDevice(vkDevice),
        m_pAllocator(pAllocator) {}
    ~CUniqueVkDevice()
    {
        Destroy();
    }

    std::pair<VkDevice, const VkAllocationCallbacks *> Detach()
    {
        std::pair<VkDevice, const VkAllocationCallbacks *> result(m_VkDevice, m_pAllocator);
        m_VkDevice = VK_NULL_HANDLE;
        m_pAllocator = nullptr;
        return result;
    }

    void Attach(VkDevice vkDevice, const VkAllocationCallbacks *pAllocator = nullptr)
    {
        Detach();

        m_VkDevice = vkDevice;
        m_pAllocator = pAllocator;
    }

    void Attach(const std::pair<VkDevice, const VkAllocationCallbacks *> &args)
    {
        Attach(args.first, args.second);
    }

    CUniqueVkDevice(const CUniqueVkDevice &o) = delete;
    CUniqueVkDevice(CUniqueVkDevice &&o) :
        m_VkDevice(o.m_VkDevice),
        m_pAllocator(o.m_pAllocator)
    {
        o.Detach();
    }

    CUniqueVkDevice &operator=(const CUniqueVkDevice &o) = delete;
    CUniqueVkDevice &operator=(CUniqueVkDevice &&o)
    {
        Attach(o.m_VkDevice, o.m_pAllocator);
        o.Detach();
        return *this;
    }

    void Swap(CUniqueVkDevice &o)
    {
        VkDevice vkDevice = m_VkDevice;
        const VkAllocationCallbacks *pAllocator = m_pAllocator;

        m_VkDevice = o.m_VkDevice;
        m_pAllocator = o.m_pAllocator;

        o.m_VkDevice = vkDevice;
        o.m_pAllocator = pAllocator;
    }

    VkDevice Get() const { return m_VkDevice; }

    VkDevice *operator&() { return &m_VkDevice; }
    const VkDevice *operator&() const { return &m_VkDevice; }
};

#define FOR_EACH_VK_DEVICE_OBJECT_HANDLE_TYPE(macro, ...) \
    macro(Fence, ##__VA_ARGS__) \
    macro(Semaphore, ##__VA_ARGS__) \
    macro(Event, ##__VA_ARGS__) \
    macro(QueryPool, ##__VA_ARGS__) \
    macro(Buffer, ##__VA_ARGS__) \
    macro(BufferView, ##__VA_ARGS__) \
    macro(Image, ##__VA_ARGS__) \
    macro(ImageView, ##__VA_ARGS__) \
    macro(ShaderModule, ##__VA_ARGS__) \
    macro(PipelineCache, ##__VA_ARGS__) \
    macro(Pipeline, ##__VA_ARGS__) \
    macro(PipelineLayout, ##__VA_ARGS__) \
    macro(Sampler, ##__VA_ARGS__) \
    macro(DescriptorSetLayout, ##__VA_ARGS__) \
    macro(DescriptorPool, ##__VA_ARGS__) \
    macro(Framebuffer, ##__VA_ARGS__) \
    macro(RenderPass, ##__VA_ARGS__) \
    macro(CommandPool, ##__VA_ARGS__) \
    macro(SwapchainKHR, ##__VA_ARGS__) \

#define DEFINE_VK_DEVICE_OBJECT_DESTROY_FUNC(type, ...) \
inline void VkDestroyDeviceObject(VkDevice device, Vk##type handle, const VkAllocationCallbacks *pcb) { vkDestroy##type(device, handle, pcb); }

FOR_EACH_VK_DEVICE_OBJECT_HANDLE_TYPE(DEFINE_VK_DEVICE_OBJECT_DESTROY_FUNC);

//------------------------------------------------------------------------------------------------
template<class _VkHandleType>
class CUniqueVkDeviceObject
{
    VkDevice m_VkDevice = VK_NULL_HANDLE;
    _VkHandleType m_VkHandle = VK_NULL_HANDLE;
    const VkAllocationCallbacks *m_pAllocator = nullptr;

    void Destroy()
    {
        if (m_VkHandle != VK_NULL_HANDLE)
        {
            VkDestroyDeviceObject(m_VkDevice, m_VkHandle, m_pAllocator);
            Detach();
        }
    }

public:
    std::tuple<_VkHandleType, VkDevice, const VkAllocationCallbacks *> Detach()
    {
        std::tuple<_VkHandleType, VkDevice, const VkAllocationCallbacks *> result(m_VkHandle, m_VkDevice, m_pAllocator);
        m_VkHandle = VK_NULL_HANDLE;
        m_VkDevice = VK_NULL_HANDLE;
        m_pAllocator = nullptr;
        return result;
    }

    void Attach(_VkHandleType _VkHandleType, VkDevice vkDevice, const VkAllocationCallbacks *pAllocator = nullptr)
    {
        Detach();

        m_VkHandle = _VkHandleType;
        m_VkDevice = vkDevice;
        m_pAllocator = pAllocator;
    }

    void Attach(const std::tuple<_VkHandleType, VkDevice, const VkAllocationCallbacks *> &args)
    {
        Attach(std::get<0>(args), std::get<1>(args), std::get<2>(args));
    }

    CUniqueVkDeviceObject() = default;
    CUniqueVkDeviceObject(_VkHandleType vkHandle, VkDevice vkDevice, const VkAllocationCallbacks *pAllocator = nullptr) :
        m_VkHandle(vkHandle),
        m_VkDevice(vkDevice),
        m_pAllocator(pAllocator) {}
    CUniqueVkDeviceObject(const CUniqueVkDeviceObject &o) = delete;
    CUniqueVkDeviceObject(CUniqueVkDeviceObject &&o) :
        m_VkHandle(o.m_VkHandle),
        m_VkDevice(o.m_VkDevice),
        m_pAllocator(o.m_pAllocator)
    {
        o.Detach();
    }
    ~CUniqueVkDeviceObject()
    {
        Destroy();
    }

    CUniqueVkDeviceObject &operator=(const CUniqueVkDeviceObject &o) = delete;
    CUniqueVkDeviceObject &operator=(CUniqueVkDeviceObject &&o)
    {
        Destroy();

        Attach(o.m_VkHandle, o.m_VkDevice, o.m_pAllocator);
        o.Detach();
    }

    void Swap(CUniqueVkDeviceObject &o)
    {
        VkDevice vkDevice = m_VkDevice;
        _VkHandleType vkHandle = m_VkHandle;
        const VkAllocationCallbacks *pAllocator = m_pAllocator;

        m_VkDevice = o.m_VkDevice;
        m_VkHandle = o.m_VkHandle;
        m_pAllocator = o.m_pAllocator;

        o.m_VkDevice = vkDevice;
        o.m_VkHandle = vkHandle;
        o.m_pAllocator = pAllocator;
    }

    VkDevice Device() const { return m_VkDevice; }

    _VkHandleType Get() const { return m_VkHandle; }
};

#define DEFINE_UNIQUE_VK_HANDLE_WRAPPER(type, ...) \
using UniqueVk##type = CUniqueVkDeviceObject<Vk##type>;

FOR_EACH_VK_DEVICE_OBJECT_HANDLE_TYPE(DEFINE_UNIQUE_VK_HANDLE_WRAPPER);

//------------------------------------------------------------------------------------------------
class CFenceVk
{
    UniqueVkFence m_vkFence;

public:
    CFenceVk() = default;
    CFenceVk(VkFence fence, VkDevice vkDevice, const VkAllocationCallbacks *pAllocator) :
        m_vkFence(fence, vkDevice, pAllocator) {}

    VkFence GetVkFence() const { return m_vkFence.Get(); }
};
