//================================================================================================
// CUploadRing - Implementation (port of CDevice12's upload-ring loose members).
//================================================================================================

#include "pch.h"
#include "UploadRing.h"
#include "Device12.h"

//------------------------------------------------------------------------------------------------
void CUploadRing::Initialize(CDevice12* pDevice, uint64_t initialSize)
{
    m_pDevice = pDevice;
    m_Size = initialSize;
    m_WriteOffset = 0;
    m_ReadOffset = 0;
    m_LastCompletedFenceValue = 0;
    m_FrameMarkers.clear();
    // Resource creation is lazy — EnsureResource() on first allocate.
}

//------------------------------------------------------------------------------------------------
void CUploadRing::Shutdown()
{
    if (m_pResource)
    {
        m_pResource->Unmap(0, nullptr);
        m_pResource.Release();
    }
    m_pMapped = nullptr;
    m_GpuBase = 0;
    m_Size = 0;
    m_WriteOffset = 0;
    m_ReadOffset = 0;
    m_FrameMarkers.clear();
    m_pDevice = nullptr;
}

//------------------------------------------------------------------------------------------------
void CUploadRing::EnsureResource()
{
    if (m_pResource)
        return;

    D3D12_RESOURCE_DESC1 bufDesc = {};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = m_Size;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type             = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask  = 1;

    ThrowFailedHResult(m_pDevice->GetD3DDevice()->CreateCommittedResource3(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, nullptr, 0, nullptr,
        IID_PPV_ARGS(&m_pResource)));
    m_pResource->SetName(L"CanvasGfx_UploadRing");

    void* pMapped = nullptr;
    ThrowFailedHResult(m_pResource->Map(0, nullptr, &pMapped));
    m_pMapped = static_cast<uint8_t*>(pMapped);
    m_GpuBase = m_pResource->GetGPUVirtualAddress();
}

//------------------------------------------------------------------------------------------------
Gem::Result CUploadRing::AllocateFromRing(uint64_t sizeInBytes, HostWriteAllocation& out)
{
    if (sizeInBytes == 0)
        return Gem::Result::InvalidArg;

    try
    {
        constexpr uint64_t kAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        uint64_t alignedSize = (sizeInBytes + kAlignment - 1) & ~(kAlignment - 1);

        EnsureResource();

        auto availableBytes = [&]() -> uint64_t {
            if (m_WriteOffset >= m_ReadOffset)
                return m_Size - m_WriteOffset + m_ReadOffset;
            return m_ReadOffset - m_WriteOffset;
        };

        uint64_t available = availableBytes();

        // Try reclaiming completed submissions first.
        if (alignedSize > available)
        {
            Reclaim(m_LastCompletedFenceValue);
            available = availableBytes();
        }

        // Grow if still insufficient.  Double the ring (at minimum) until the
        // request fits.
        if (alignedSize > available)
        {
            uint64_t needed = m_Size ? m_Size : alignedSize;
            while (needed < alignedSize)
                needed *= 2;
            GrowTo(needed);
        }

        // Handle wrap-around.
        if (m_WriteOffset + alignedSize > m_Size)
        {
            if (alignedSize > m_ReadOffset)
                GrowTo(m_Size * 2);
            else
                m_WriteOffset = 0;
        }

        out.GpuAddress     = m_GpuBase + m_WriteOffset;
        out.pMapped        = m_pMapped + m_WriteOffset;
        out.Size           = sizeInBytes;
        out.pResource      = m_pResource;
        out.ResourceOffset = m_WriteOffset;

        m_WriteOffset += alignedSize;
        return Gem::Result::Success;
    }
    catch (const Gem::GemError& e) { return e.Result(); }
    catch (const _com_error& e)    { return ResultFromHRESULT(e.Error()); }
}

//------------------------------------------------------------------------------------------------
void CUploadRing::MarkSubmissionEnd(UINT64 fenceValue)
{
    m_FrameMarkers.push_back({ fenceValue, m_WriteOffset });

    // Tag any retired resources whose retiring submission is the one we're
    // about to signal. A retired resource may have been referenced by commands
    // recorded since it was released and up through this MarkSubmissionEnd, so
    // it must outlive `fenceValue`.
    for (auto& r : m_RetiredResources)
    {
        if (r.FenceValue == 0)
            r.FenceValue = fenceValue;
    }
}

//------------------------------------------------------------------------------------------------
void CUploadRing::Reclaim(UINT64 completedFenceValue)
{
    m_LastCompletedFenceValue = completedFenceValue;
    while (!m_FrameMarkers.empty())
    {
        auto& oldest = m_FrameMarkers.front();
        if (oldest.FenceValue > completedFenceValue)
            break;
        m_ReadOffset = oldest.WriteOffset;
        m_FrameMarkers.pop_front();
    }

    // Free retired backing resources whose tagged fence has retired. Untagged
    // entries (FenceValue == 0) are still pending submission and must wait.
    while (!m_RetiredResources.empty())
    {
        auto& oldest = m_RetiredResources.front();
        if (oldest.FenceValue == 0 || oldest.FenceValue > completedFenceValue)
            break;
        m_RetiredResources.pop_front();
    }
}

//------------------------------------------------------------------------------------------------
void CUploadRing::GrowTo(uint64_t newSize)
{
    if (m_pResource)
    {
        m_pResource->Unmap(0, nullptr);

        // Hold the old resource alive until the next signalled submission has
        // retired. Allocations made before this GrowTo handed out raw GPU
        // virtual addresses and ID3D12Resource* pointers into this resource;
        // those must remain valid until the GPU is done with them.
        m_RetiredResources.push_back({ /*FenceValue*/ 0, m_pResource });

        m_pResource.Release();
        m_pMapped = nullptr;
        m_GpuBase = 0;
    }

    m_Size = newSize;
    m_WriteOffset = 0;
    m_ReadOffset = 0;
    m_FrameMarkers.clear();
    EnsureResource();
}
