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
    m_InFlightBytes = 0;
    m_BytesAllocatedThisFrame = 0;
    m_LastCompletedFenceValue = 0;
    m_FrameMarkers.clear();
    // Resource creation is lazy - EnsureResource() on first allocate.
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
    m_InFlightBytes = 0;
    m_BytesAllocatedThisFrame = 0;
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
    SetD3D12DebugName(m_pResource, "CanvasGfx_UploadRing");

    void* pMapped = nullptr;
    ThrowFailedHResult(m_pResource->Map(0, nullptr, &pMapped));
    m_pMapped = static_cast<uint8_t*>(pMapped);
    m_GpuBase = m_pResource->GetGPUVirtualAddress();
}

//------------------------------------------------------------------------------------------------
Gem::Result CUploadRing::AllocateFromRing(uint64_t sizeInBytes, HostWriteAllocation& out, uint64_t alignment)
{
    if (sizeInBytes == 0)
        return Gem::Result::InvalidArg;
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        return Gem::Result::InvalidArg;

    try
    {
        const uint64_t alignMask = alignment - 1;
        const uint64_t alignedSize = (sizeInBytes + alignMask) & ~alignMask;

        EnsureResource();

        // Single source of truth.  No head==tail ambiguity: full <=>
        // m_InFlightBytes == m_Size; empty <=> m_InFlightBytes == 0.
        auto availableBytes = [&]() -> uint64_t {
            return m_Size - m_InFlightBytes;
        };

        // Pad m_WriteOffset up to the requested alignment before measuring fit.
        auto alignedWrite = [&]() -> uint64_t {
            return (m_WriteOffset + alignMask) & ~alignMask;
        };

        uint64_t writeOffset = alignedWrite();
        uint64_t padHead     = writeOffset - m_WriteOffset;
        uint64_t needed      = padHead + alignedSize;

        // Try reclaiming completed submissions first.
        if (needed > availableBytes())
        {
            Reclaim(m_LastCompletedFenceValue);
        }

        // Grow if still insufficient.
        if (needed > availableBytes())
        {
            uint64_t target = m_Size ? m_Size : (alignedSize + alignment);
            while (target < alignedSize + alignment)
                target *= 2;
            GrowTo(target);
            writeOffset = alignedWrite();
            padHead     = writeOffset - m_WriteOffset;
        }

        // Handle wrap-around (the aligned start + payload doesn't fit
        // before m_Size).  The slack from m_WriteOffset to m_Size is
        // abandoned by the wrap; charge it to in-flight bytes so it is
        // reclaimed alongside this frame's data (one MarkSubmissionEnd
        // later) rather than appearing as free space that doesn't
        // exist.
        uint64_t wrapSlack = 0;
        if (writeOffset + alignedSize > m_Size)
        {
            if (alignedSize + alignment > m_ReadOffset)
            {
                GrowTo(m_Size * 2);
                writeOffset = alignedWrite();
            }
            else
            {
                wrapSlack    = m_Size - m_WriteOffset;
                m_WriteOffset = 0;
                writeOffset   = 0;  // already a multiple of alignment
            }
            padHead = writeOffset - m_WriteOffset;
        }

        out.GpuAddress     = m_GpuBase + writeOffset;
        out.pMapped        = m_pMapped + writeOffset;
        out.Size           = sizeInBytes;
        out.pResource      = m_pResource;
        out.ResourceOffset = writeOffset;

        m_WriteOffset = writeOffset + alignedSize;
        const uint64_t bytesConsumed = padHead + alignedSize + wrapSlack;
        m_InFlightBytes           += bytesConsumed;
        m_BytesAllocatedThisFrame += bytesConsumed;
        return Gem::Result::Success;
    }
    catch (const Gem::GemError& e) { return e.Result(); }
    catch (const _com_error& e)    { return ResultFromHRESULT(e.Error()); }
}

//------------------------------------------------------------------------------------------------
void CUploadRing::MarkSubmissionEnd(UINT64 fenceValue)
{
    m_FrameMarkers.push_back({ fenceValue, m_WriteOffset, m_BytesAllocatedThisFrame });
    m_BytesAllocatedThisFrame = 0;

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
    // Advance the read pointer (and shrink the in-flight count) by the
    // size of each fully-retired frame's footprint.  Using BytesInFrame
    // instead of jumping m_ReadOffset to the marker's recorded
    // WriteOffset keeps the read pointer monotonically advancing through
    // the ring (modulo wrap), which prevents the post-wrap regression
    // that the WriteOffset-jump approach suffered from when a wrap'd
    // frame's offset was numerically smaller than the previous
    // frame's.
    while (!m_FrameMarkers.empty())
    {
        auto& oldest = m_FrameMarkers.front();
        if (oldest.FenceValue > completedFenceValue)
            break;
        m_InFlightBytes -= oldest.BytesInFrame;
        m_ReadOffset     = (m_ReadOffset + oldest.BytesInFrame) % m_Size;
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
    // New ring starts fully empty.  Any in-flight bytes from the old
    // ring are tracked separately via m_RetiredResources -- the new
    // ring has no relation to that memory and accounts only for its
    // own allocations from here on.
    m_InFlightBytes = 0;
    m_BytesAllocatedThisFrame = 0;
    m_FrameMarkers.clear();
    EnsureResource();
}

