//================================================================================================
// CBufferPool - Implementation
//================================================================================================

#include "pch.h"
#include "BufferPool.h"
#include "Buffer12.h"

//------------------------------------------------------------------------------------------------
bool CBufferPool::IsPoolable(uint64_t sizeInBytes)
{
    return RoundUpPow2(sizeInBytes) <= (1ULL << kMaxBucketLog2);
}

//------------------------------------------------------------------------------------------------
uint32_t CBufferPool::BucketIndex(uint64_t capacity)
{
    // capacity is expected to be a power of 2
    uint32_t log2 = static_cast<uint32_t>(Log2Ceil(static_cast<unsigned __int64>(capacity)));
    if (log2 < kMinBucketLog2)
        return 0;
    if (log2 > kMaxBucketLog2)
        return kNumBuckets; // out-of-range sentinel
    return log2 - kMinBucketLog2;
}

//------------------------------------------------------------------------------------------------
bool CBufferPool::Acquire(uint64_t sizeInBytes, Canvas::GfxResourceAllocation& out)
{
    uint64_t capacity = RoundUpPow2(sizeInBytes);
    uint32_t bucket = BucketIndex(capacity);
    if (bucket >= kNumBuckets)
        return false;

    auto& available = m_Available[bucket];
    if (available.empty())
        return false;

    out = std::move(available.back());
    available.pop_back();
    return true;
}

//------------------------------------------------------------------------------------------------
void CBufferPool::Retire(Canvas::GfxResourceAllocation&& alloc, UINT64 fenceValue)
{
    if (!alloc.pBuffer)
        return;

    // Derive the bucket from the D3D12 resource's actual capacity, not the
    // logical Size field (which may have been set to a smaller data size).
    auto* pBuf = static_cast<CBuffer12*>(alloc.pBuffer.Get());
    uint64_t capacity = pBuf->GetD3DResource()->GetDesc().Width;
    uint32_t bucket = BucketIndex(capacity);

    if (bucket >= kNumBuckets)
        return; // Too large to pool — ref drops normally, CBuffer12 destructor frees it.

    m_RetiredBuffers.push_back({ fenceValue, bucket, std::move(alloc) });
}

//------------------------------------------------------------------------------------------------
void CBufferPool::Reclaim(UINT64 completedFenceValue)
{
    while (!m_RetiredBuffers.empty())
    {
        auto& front = m_RetiredBuffers.front();
        if (front.FenceValue > completedFenceValue)
            break;

        auto& available = m_Available[front.Bucket];
        if (available.size() < kMaxPerBucket)
            available.push_back(std::move(front.Allocation));
        // else: evict — GfxResourceAllocation destructor releases the CBuffer12

        m_RetiredBuffers.pop_front();
    }
}

//------------------------------------------------------------------------------------------------
void CBufferPool::ReleaseAll()
{
    m_RetiredBuffers.clear();
    for (auto& bucket : m_Available)
        bucket.clear();
}
