//================================================================================================
// CBufferPool - Power-of-2 bucketed pool for frequently-changing GPU buffers
//
// Reuses GPU buffer allocations to avoid per-frame ResourceAllocator churn.
// Buffers are retired with a fence value and become available once the GPU has finished
// using them.  Each bucket holds buffers of a single power-of-2 capacity.
//================================================================================================

#pragma once

#include "CanvasGfx.h"
#include "BuddySuballocator.h"   // Log2Ceil, BitScanMSB64
#include <deque>
#include <vector>
#include <cstdint>

class CBuffer12;

class CBufferPool
{
public:

    // Try to acquire a buffer with capacity >= sizeInBytes.
    // Returns true and populates `out` if a pooled buffer is available.
    bool Acquire(uint64_t sizeInBytes, Canvas::GfxResourceAllocation& out);

    // Retire a buffer to the pool, gated on fenceValue.
    // The buffer becomes available after the GPU signals the fence.
    void Retire(Canvas::GfxResourceAllocation&& alloc, UINT64 fenceValue);

    // Move retired buffers whose fence has completed into available buckets.
    void Reclaim(UINT64 completedFenceValue);

    // Release all pooled buffers (must be called before device destruction to
    // break the CDevice12 -> pool -> CBuffer12 -> CDevice12 reference cycle).
    void ReleaseAll();

    // Round up to the next power of 2 (uses intrinsic-based Log2Ceil).
    static uint64_t RoundUpPow2(uint64_t size)
    {
        return size <= 1 ? 1 : 1ULL << Log2Ceil(static_cast<unsigned __int64>(size));
    }

    // True when the (rounded-up) size falls within the poolable bucket range.
    static bool IsPoolable(uint64_t sizeInBytes);

    static constexpr uint32_t kMinBucketLog2 = 8;                              // 256 B
    static constexpr uint32_t kMaxBucketLog2 = 16;                             // 64 KB
    static constexpr uint32_t kNumBuckets = kMaxBucketLog2 - kMinBucketLog2 + 1;

private:
    static constexpr uint32_t kMaxPerBucket = 8;

    // Return the bucket index for an already-power-of-2 capacity.
    // Returns kNumBuckets (out-of-range sentinel) when the capacity is too large.
    static uint32_t BucketIndex(uint64_t capacity);

    struct RetiredBuffer
    {
        UINT64 FenceValue;
        uint32_t Bucket;
        Canvas::GfxResourceAllocation Allocation;
    };

    std::deque<RetiredBuffer> m_RetiredBuffers;
    std::vector<Canvas::GfxResourceAllocation> m_Available[kNumBuckets];
};
