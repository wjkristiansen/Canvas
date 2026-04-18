//================================================================================================
// CResourceManager - Implementation
//================================================================================================

#include "pch.h"
#include "ResourceManager.h"
#include "Buffer12.h"
#include "Device12.h"
#include "BuddySuballocator.h"   // Log2Ceil

//------------------------------------------------------------------------------------------------
CResourceManager::~CResourceManager()
{
    // Safety net: if someone forgot to call Shutdown explicitly, clear here.
    // At this point the device is being destroyed, so every queue must already
    // have been uninitialized and drained.  Move out under the lock then drop
    // the lock before destroying contents — CBuffer12 dtors call back into
    // Free() which takes the same mutex.
    std::vector<std::unique_ptr<FenceTimeline>> timelines;
    std::vector<Canvas::GfxResourceAllocation> available[kNumBuckets];
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        timelines = std::move(m_Timelines);
        for (uint32_t i = 0; i < kNumBuckets; ++i)
            available[i] = std::move(m_Available[i]);
    }
    // Locals destruct here without the lock held.
}

//------------------------------------------------------------------------------------------------
void CResourceManager::Initialize(CDevice12* pDevice)
{
    m_pDevice = pDevice;
    m_Allocator.Initialize(pDevice);
}

//------------------------------------------------------------------------------------------------
void CResourceManager::Shutdown()
{
    // Move state out while holding the lock, then drop the lock before the
    // destructors run.  CBuffer12::~CBuffer12 calls back into Free() which
    // takes m_Mutex — destructing under the lock would deadlock.
    std::vector<std::unique_ptr<FenceTimeline>> timelines;
    std::vector<Canvas::GfxResourceAllocation> available[kNumBuckets];
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        timelines = std::move(m_Timelines);
        for (uint32_t i = 0; i < kNumBuckets; ++i)
            available[i] = std::move(m_Available[i]);
    }
    // Locals destruct here; Free() reentrancy is safe without the lock.
}

//------------------------------------------------------------------------------------------------
uint32_t CResourceManager::RegisterTimeline(ID3D12Fence* pFence)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    auto pTimeline = std::make_unique<FenceTimeline>();
    pTimeline->pFence = pFence;
    pTimeline->CompletedValue = pFence ? pFence->GetCompletedValue() : 0;

    // Reuse the first empty slot if one exists.
    for (size_t i = 0; i < m_Timelines.size(); ++i)
    {
        if (!m_Timelines[i])
        {
            m_Timelines[i] = std::move(pTimeline);
            return static_cast<uint32_t>(i);
        }
    }

    m_Timelines.push_back(std::move(pTimeline));
    return static_cast<uint32_t>(m_Timelines.size() - 1);
}

//------------------------------------------------------------------------------------------------
void CResourceManager::UnregisterTimeline(uint32_t timelineId)
{
    std::unique_ptr<FenceTimeline> dead;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (timelineId >= m_Timelines.size() || !m_Timelines[timelineId])
            return;

        // Detach — caller guarantees the GPU is idle on this timeline.
        dead = std::move(m_Timelines[timelineId]);
    }
    // dead's RetiredBuffers destruct here without the lock held (Free reentrancy safe).
}

//------------------------------------------------------------------------------------------------
void CResourceManager::Reclaim()
{
    // Scratch storage is thread_local so capacity amortizes across calls.  Contents
    // are populated under the lock and destructed outside it: CBuffer12 dtors reenter
    // Free() on the manager mutex.
    thread_local std::vector<Canvas::GfxResourceAllocation> s_RecycleOverflow;
    thread_local std::vector<Gem::TGemPtr<Gem::XGeneric>> s_DeferredToDrop;

    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        for (auto& pTimeline : m_Timelines)
        {
            if (!pTimeline || !pTimeline->pFence)
                continue;

            const UINT64 completed = pTimeline->pFence->GetCompletedValue();
            pTimeline->CompletedValue = completed;

            // Drain retired buffers — recycle into the available pool when there's room.
            while (!pTimeline->RetiredBuffers.empty()
                && pTimeline->RetiredBuffers.front().Value <= completed)
            {
                auto& front = pTimeline->RetiredBuffers.front();
                if (front.Bucket < kNumBuckets && m_Available[front.Bucket].size() < kMaxPerBucket)
                    m_Available[front.Bucket].push_back(std::move(front.Allocation));
                else
                    s_RecycleOverflow.push_back(std::move(front.Allocation));
                pTimeline->RetiredBuffers.pop_front();
            }

            // Drain deferred resources — move refs out, destruct outside the lock.
            while (!pTimeline->DeferredResources.empty()
                && pTimeline->DeferredResources.front().Value <= completed)
            {
                s_DeferredToDrop.push_back(std::move(pTimeline->DeferredResources.front().pResource));
                pTimeline->DeferredResources.pop_front();
            }
        }
    }

    s_RecycleOverflow.clear();
    s_DeferredToDrop.clear();
}

//------------------------------------------------------------------------------------------------
uint64_t CResourceManager::RoundUpPow2(uint64_t size)
{
    return size <= 1 ? 1 : 1ULL << Log2Ceil(static_cast<unsigned __int64>(size));
}

//------------------------------------------------------------------------------------------------
bool CResourceManager::IsPoolableSize(uint64_t sizeInBytes)
{
    return RoundUpPow2(sizeInBytes) <= (1ULL << kMaxBucketLog2);
}

//------------------------------------------------------------------------------------------------
uint32_t CResourceManager::BucketIndex(uint64_t capacity)
{
    // capacity is expected to already be a power of 2.
    uint32_t log2 = static_cast<uint32_t>(Log2Ceil(static_cast<unsigned __int64>(capacity)));
    if (log2 < kMinBucketLog2)
        return 0;
    if (log2 > kMaxBucketLog2)
        return kNumBuckets;  // out-of-range sentinel
    return log2 - kMinBucketLog2;
}

//------------------------------------------------------------------------------------------------
bool CResourceManager::AcquireBuffer(uint64_t sizeInBytes, Canvas::GfxResourceAllocation& out)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

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
void CResourceManager::RetireBuffer(Canvas::GfxResourceAllocation&& alloc, FenceToken token)
{
    if (!alloc.pBuffer)
        return;

    // Derive bucket from the underlying D3D12 resource's actual capacity, not from
    // the logical Size field (which may have been narrowed for the last upload).
    auto* pBuf = static_cast<CBuffer12*>(alloc.pBuffer.Get());
    uint64_t capacity = pBuf->GetD3DResource()->GetDesc().Width;
    uint32_t bucket = BucketIndex(capacity);

    if (bucket >= kNumBuckets)
        return;  // Too large to pool — drop the ref; CBuffer12 destructor frees the allocator block.

    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!token.IsValid() || token.TimelineId >= m_Timelines.size() || !m_Timelines[token.TimelineId])
    {
        // No valid timeline (or timeline already torn down) — drop the buffer immediately.
        // Caller is responsible for having drained the GPU before unregistering a timeline.
        return;
    }

    auto& timeline = *m_Timelines[token.TimelineId];
    timeline.RetiredBuffers.push_back({ token.Value, bucket, std::move(alloc) });
}

//------------------------------------------------------------------------------------------------
void CResourceManager::DeferRelease(Gem::TGemPtr<Gem::XGeneric>&& pResource, FenceToken token)
{
    if (!pResource)
        return;

    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!token.IsValid() || token.TimelineId >= m_Timelines.size() || !m_Timelines[token.TimelineId])
    {
        // No valid timeline — drop the ref immediately.  Caller is responsible
        // for draining before tearing down the timeline.
        return;
    }

    auto& timeline = *m_Timelines[token.TimelineId];
    timeline.DeferredResources.push_back({ token.Value, std::move(pResource) });
}

//------------------------------------------------------------------------------------------------
Gem::Result CResourceManager::Alloc(
    const D3D12_RESOURCE_DESC& desc,
    D3D12_BARRIER_LAYOUT initialLayout,
    ResourceAllocation& out,
    const char* name)
{
    // CResourceAllocator is unsynchronized (matches pre-migration behavior —
    // callers must external-serialize Alloc/Free, typically on the device init
    // thread for persistent buffers).
    return m_Allocator.Alloc(desc, initialLayout, out, name);
}

//------------------------------------------------------------------------------------------------
void CResourceManager::Free(ResourceAllocation& allocation)
{
    m_Allocator.Free(allocation);
}
