//================================================================================================
// CResourceManager - Device-level, queue-agnostic GPU resource lifecycle manager.
//
// Owns:
//   - CResourceAllocator (placed + committed allocations)
//   - Power-of-2 bucketed buffer pool
//   - Per-timeline deferred release and retired-buffer queues
//
// Every queue registers an ID3D12Fence and receives a stable TimelineId.  Deferred
// operations carry a FenceToken {TimelineId, FenceValue} instead of a bare UINT64,
// so the manager correctly retires/releases across any mix of render/compute/copy queues.
//================================================================================================

#pragma once

#include "ResourceAllocator.h"
#include <d3d12.h>
#include <atlbase.h>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <Gem.hpp>

namespace Gem { struct XGeneric; }
namespace Canvas { struct GfxResourceAllocation; }

class CDevice12;

//------------------------------------------------------------------------------------------------
// FenceToken - lightweight {timeline, value} handle passed by value.
//
// TimelineId indexes into CResourceManager::m_Timelines.  The ID3D12Fence* COM
// pointer lives once in the FenceTimeline, not duplicated per token.
struct FenceToken
{
    uint32_t TimelineId = kInvalidTimelineId;
    UINT64   Value      = 0;

    static constexpr uint32_t kInvalidTimelineId = UINT32_MAX;

    bool IsValid() const { return TimelineId != kInvalidTimelineId; }
};

//------------------------------------------------------------------------------------------------
// FenceTimeline - per-queue retirement bucket.
//
// Holds one ID3D12Fence COM ref plus the retired-buffer and deferred-release deques
// for that queue.  Values within each deque are monotonically increasing by construction
// (one producer per timeline) so a front-to-back pop correctly drains completed entries.
struct FenceTimeline
{
    CComPtr<ID3D12Fence> pFence;
    UINT64 CompletedValue = 0;   // Cached, refreshed once per Reclaim pass.

    struct RetiredBuffer
    {
        UINT64 Value;
        uint32_t Bucket;
        Canvas::GfxResourceAllocation Allocation;
    };
    std::deque<RetiredBuffer> RetiredBuffers;

    struct DeferredRef
    {
        UINT64 Value;
        Gem::TGemPtr<Gem::XGeneric> pResource;
    };
    std::deque<DeferredRef> DeferredResources;
};

//------------------------------------------------------------------------------------------------
class CResourceManager
{
public:
    CResourceManager() = default;
    ~CResourceManager();

    // Non-copyable / non-movable — pinned by pointer from CDevice12.
    CResourceManager(const CResourceManager&) = delete;
    CResourceManager& operator=(const CResourceManager&) = delete;

    void Initialize(CDevice12* pDevice);

    // Drain all timelines and clear buffer pool buckets.  Breaks the
    // CDevice12 -> pool -> CBuffer12 -> CDevice12 reference cycle.
    // Caller must ensure the GPU is idle on every registered timeline first.
    void Shutdown();

    // Register a queue's fence and obtain a stable timeline id.  The manager
    // AddRefs the fence for the lifetime of the timeline.
    uint32_t RegisterTimeline(ID3D12Fence* pFence);

    // Drain and remove a timeline.  Caller must ensure the fence has reached
    // the highest value ever submitted through this timeline before calling.
    void UnregisterTimeline(uint32_t timelineId);

    // Scan every timeline, refresh completed values, drain retired buffers and
    // deferred resources whose fence value has been reached.
    void Reclaim();

    //--------------------------------------------------------------------------------------------
    // Buffer pool (power-of-2 bucketed recycling for short-lived buffers).
    //
    // Pooled allocations are buffers only — textures must use placed/committed allocation.
    //--------------------------------------------------------------------------------------------

    // Try to acquire a buffer with capacity >= sizeInBytes from the available pool.
    // Returns true and populates `out` on hit; false on miss.
    bool AcquireBuffer(uint64_t sizeInBytes, Canvas::GfxResourceAllocation& out);

    // Retire a previously-acquired buffer.  It becomes available for reuse once
    // the fence at `token` has been reached on its registered timeline.
    void RetireBuffer(Canvas::GfxResourceAllocation&& alloc, FenceToken token);

    // Defer release of any GEM ref-counted object until the fence at `token`
    // has been reached on its registered timeline.  Drops the ref silently if
    // the timeline is invalid (caller responsibility to drain before unregister).
    void DeferRelease(Gem::TGemPtr<Gem::XGeneric>&& pResource, FenceToken token);

    //--------------------------------------------------------------------------------------------
    // Placed / committed resource allocation (thin forwarder over CResourceAllocator).
    //--------------------------------------------------------------------------------------------

    Gem::Result Alloc(
        const D3D12_RESOURCE_DESC& desc,
        D3D12_BARRIER_LAYOUT initialLayout,
        ResourceAllocation& out,
        const char* name = nullptr);

    void Free(ResourceAllocation& allocation);

    // True when the (rounded-up) size falls within the poolable bucket range.
    static bool IsPoolableSize(uint64_t sizeInBytes);

    // Round up to the next power of 2.
    static uint64_t RoundUpPow2(uint64_t size);

    // Bucket configuration.
    static constexpr uint32_t kMinBucketLog2 = 8;                              // 256 B
    static constexpr uint32_t kMaxBucketLog2 = 16;                             // 64 KB
    static constexpr uint32_t kNumBuckets    = kMaxBucketLog2 - kMinBucketLog2 + 1;

private:
    static constexpr uint32_t kMaxPerBucket = 8;

    // Returns the bucket index for an already-power-of-2 capacity, or
    // kNumBuckets when out of range.
    static uint32_t BucketIndex(uint64_t capacity);

    CDevice12* m_pDevice = nullptr;

    CResourceAllocator m_Allocator;

    std::vector<std::unique_ptr<FenceTimeline>> m_Timelines;  // indexed by TimelineId; entries may be null after Unregister

    // Shared free pool — populated by Reclaim() when retired buffers complete.
    std::vector<Canvas::GfxResourceAllocation> m_Available[kNumBuckets];

    std::mutex m_Mutex;
};
