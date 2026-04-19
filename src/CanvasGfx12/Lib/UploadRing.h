//================================================================================================
// CUploadRing - Per-queue linear upload ring.
//
// A single committed UPLOAD-heap ID3D12Resource, persistently mapped.  Suballocations
// advance a write offset monotonically; space is reclaimed in bulk per queue submission
// via {fenceValue, writeOffset} markers.
//
// Bare UINT64 fence values are acceptable here because the ring is private to exactly
// one queue which owns exactly one fence.  (Contrast with device-level resource
// lifecycle, which must use FenceToken to disambiguate across queues.)
//================================================================================================

#pragma once

#include <d3d12.h>
#include <atlbase.h>
#include <cstdint>
#include <deque>
#include <Gem.hpp>

class CDevice12;

//------------------------------------------------------------------------------------------------
// Lightweight ring buffer suballocation.  No ref counting; the ring owns the resource.
struct HostWriteAllocation
{
    D3D12_GPU_VIRTUAL_ADDRESS GpuAddress = 0;
    void* pMapped = nullptr;
    uint64_t Size = 0;
    ID3D12Resource* pResource = nullptr;   // Raw ring-buffer resource (for CopyBufferRegion source).
    uint64_t ResourceOffset = 0;           // Offset within the ring-buffer resource.
};

//------------------------------------------------------------------------------------------------
class CUploadRing
{
public:
    CUploadRing() = default;
    ~CUploadRing() { Shutdown(); }

    CUploadRing(const CUploadRing&) = delete;
    CUploadRing& operator=(const CUploadRing&) = delete;

    // Allocate the backing UPLOAD resource.  Called lazily on first AllocateFromRing,
    // or explicitly from CRenderQueue12 initialization.
    void Initialize(CDevice12* pDevice, uint64_t initialSize);

    // Release the backing resource and reset all offsets.
    void Shutdown();

    // Carve sizeInBytes out of the ring.  Both size and the returned offset are
    // padded up to `alignment` bytes (must be a power of two).  Default alignment
    // is D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT (512) which is a strict superset
    // of D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT (256), so the returned
    // ResourceOffset is valid for either CBVs or D3D12_PLACED_SUBRESOURCE_FOOTPRINT::Offset.
    // Grows the ring if there is not enough free space even after reclaiming
    // completed submissions.
    Gem::Result AllocateFromRing(
        uint64_t sizeInBytes,
        HostWriteAllocation& out,
        uint64_t alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

    // Record the write offset at the point the owning queue signalled fenceValue.
    // Space consumed prior to this marker becomes reclaimable once that fence completes.
    void MarkSubmissionEnd(UINT64 fenceValue);

    // Advance the read pointer over every marker whose fence value has completed.
    void Reclaim(UINT64 completedFenceValue);

    // Cached completion for mid-allocation reclaim attempts.
    void SetLastCompletedFenceValue(UINT64 value) { m_LastCompletedFenceValue = value; }

private:
    void EnsureResource();
    void GrowTo(uint64_t newSize);

    CDevice12* m_pDevice = nullptr;

    CComPtr<ID3D12Resource> m_pResource;
    D3D12_GPU_VIRTUAL_ADDRESS m_GpuBase = 0;
    uint8_t* m_pMapped = nullptr;

    uint64_t m_Size        = 0;
    uint64_t m_WriteOffset = 0;
    uint64_t m_ReadOffset  = 0;
    UINT64   m_LastCompletedFenceValue = 0;

    struct FrameMarker
    {
        UINT64 FenceValue;
        uint64_t WriteOffset;
    };
    std::deque<FrameMarker> m_FrameMarkers;

    // Old backing resources released by GrowTo. Held alive until the queue
    // fence reaches the submission that referenced them. FenceValue == 0 means
    // "still pending; tag with the next MarkSubmissionEnd value".
    struct RetiredResource
    {
        UINT64 FenceValue;
        CComPtr<ID3D12Resource> pResource;
    };
    std::deque<RetiredResource> m_RetiredResources;
};
