//================================================================================================
// PipelineBarrier
//================================================================================================

#pragma once

struct SubresourceState
{

};

//------------------------------------------------------------------------------------------------
void QuickImageMemoryBarrier(
    VkCommandBuffer commandBuffer,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    VkDependencyFlags depFlags,
    const VkImageMemoryBarrier &barrier)
{
    vkCmdPipelineBarrier(
        commandBuffer,
        srcStageMask,
        dstStageMask,
        depFlags,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

//------------------------------------------------------------------------------------------------
void QuickMemoryBarrier(
    VkCommandBuffer commandBuffer,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    VkDependencyFlags depFlags,
    const VkMemoryBarrier &barrier)
{
    vkCmdPipelineBarrier(
        commandBuffer,
        srcStageMask,
        dstStageMask,
        depFlags,
        1,
        &barrier,
        0,
        nullptr,
        0,
        nullptr
    );
}

//------------------------------------------------------------------------------------------------
void QuickBufferMemoryBarrier(
    VkCommandBuffer commandBuffer,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    VkDependencyFlags depFlags,
    const VkBufferMemoryBarrier &barrier)
{
    vkCmdPipelineBarrier(
        commandBuffer,
        srcStageMask,
        dstStageMask,
        depFlags,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr
    );
}

//------------------------------------------------------------------------------------------------
class CPipelineBarrierBuilder
{
    std::vector<VkImageMemoryBarrier> m_imageMemoryBarriers;
    std::vector<VkBufferMemoryBarrier> m_bufferMemoryBarriers;
    std::vector<VkMemoryBarrier> m_memoryBarriers;

    VkPipelineStageFlags m_srcStageMask = 0;
    VkPipelineStageFlags m_dstStageMask = 0;
    VkDependencyFlags m_depFlags = 0;

    CPipelineBarrierBuilder() = default;
    CPipelineBarrierBuilder(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags depFlags) :
        m_srcStageMask(srcStageMask),
        m_dstStageMask(dstStageMask),
        m_depFlags(depFlags)
    {}

    void Reset(VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags depFlags)
    {
        m_srcStageMask = srcStageMask;
        m_dstStageMask = dstStageMask;
        m_depFlags = depFlags;

        m_imageMemoryBarriers.clear();
        m_bufferMemoryBarriers.clear();
        m_memoryBarriers.clear();
    }

    void AddMemoryBarrier(
        const VkMemoryBarrier &barrier
    )
    {
        m_memoryBarriers.push_back(barrier);
    }

    void AddBufferMemoryBarrier(
        const VkBufferMemoryBarrier &barrier
    )
    {
        m_bufferMemoryBarriers.push_back(barrier);
    }

    void AddImageMemoryBarrier(
        const VkImageMemoryBarrier &barrier
    )
    {
        m_imageMemoryBarriers.push_back(barrier);
    }

    Gem::Result ApplyBarriers(VkCommandBuffer commandBuffer)
    {
        vkCmdPipelineBarrier(
            commandBuffer,
            m_srcStageMask,
            m_dstStageMask,
            m_depFlags,
            static_cast<uint32_t>(m_memoryBarriers.size()),
            m_memoryBarriers.data(),
            static_cast<uint32_t>(m_bufferMemoryBarriers.size()),
            m_bufferMemoryBarriers.data(),
            static_cast<uint32_t>(m_imageMemoryBarriers.size()),
            m_imageMemoryBarriers.data()
        );
    }

public:
    CPipelineBarrierBuilder() = default;
};

