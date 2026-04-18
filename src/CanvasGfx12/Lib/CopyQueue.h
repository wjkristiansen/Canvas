//================================================================================================
// CCopyQueue - Device-owned D3D12 COPY-queue worker.
//
// Owns one ID3D12CommandQueue (COPY type), a fence, an allocator pool, and a CUploadRing.
// Buffer-upload requests accumulate in a small pending list; FlushIfPending records all
// pending copies into one CL, executes it, signals, and returns a FenceToken that the
// render queue (or any future consumer queue) can Wait() on before reading the destinations.
//
// Buffers have no D3D12 layout, so no barriers are required inside the copy CL: cross-queue
// fence synchronization on a COPY-queue signal is sufficient to make the writes visible to
// the consuming queue's first access.  Texture upload is out of scope until the COMMON-only
// constraint is wired through.
//================================================================================================

#pragma once

#include "CommandAllocatorPool.h"
#include "ResourceManager.h"
#include "UploadRing.h"

#include <atlbase.h>
#include <d3d12.h>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>
#include <Gem.hpp>

class CDevice12;

class CCopyQueue
{
public:
    CCopyQueue() = default;
    ~CCopyQueue() = default;

    CCopyQueue(const CCopyQueue&) = delete;
    CCopyQueue& operator=(const CCopyQueue&) = delete;

    void Initialize(CDevice12* pDevice);

    // Drains the GPU on the copy fence, unregisters the timeline, and releases all D3D12 objects.
    // Safe to call from CDevice12 destructor before m_ResourceManager.Shutdown().
    void Shutdown();

    CUploadRing& GetUploadRing() { return m_UploadRing; }

    ID3D12CommandQueue* GetD3DCommandQueue() const { return m_pCommandQueue; }
    ID3D12Fence* GetD3DFence() const { return m_pFence; }
    uint32_t GetTimelineId() const { return m_TimelineId; }

    // Last fence value signaled on the copy queue (the "high-water mark" the
    // consumer queue must Wait() for in order to see all completed uploads).
    UINT64 GetLastSignaledValue() const { return m_LastSignaledValue; }

    // Append a buffer copy to the pending list.  Source memory is expected to live
    // in this queue's upload ring (suballocated via GetUploadRing()).  pDstKeepAlive
    // is held until the copy completes via the manager's deferred-release queue on
    // this queue's timeline.
    void EnqueueBufferCopy(
        ID3D12Resource* pSrc, uint64_t srcOffset,
        ID3D12Resource* pDst, uint64_t dstOffset,
        uint64_t size,
        Gem::TGemPtr<Gem::XGeneric> pDstKeepAlive);

    // If there are pending copies, record + ECL + signal and return the fence token
    // that covers all of them.  Returns nullopt when there is nothing to flush.
    std::optional<FenceToken> FlushIfPending();

private:
    struct PendingCopy
    {
        ID3D12Resource* pSrc;
        uint64_t SrcOffset;
        ID3D12Resource* pDst;
        uint64_t DstOffset;
        uint64_t Size;
    };

    void DrainGpu();

    CDevice12* m_pDevice = nullptr;

    CComPtr<ID3D12CommandQueue>     m_pCommandQueue;
    CComPtr<ID3D12Fence>            m_pFence;
    CCommandAllocatorPool           m_AllocatorPool;
    CComPtr<ID3D12CommandAllocator> m_pAllocator;   // recycled across flushes via the pool
    CUploadRing                     m_UploadRing;

    uint32_t m_TimelineId       = FenceToken::kInvalidTimelineId;
    UINT64   m_LastSignaledValue = 0;

    std::mutex m_Mutex;
    std::vector<PendingCopy>                  m_Pending;
    std::vector<Gem::TGemPtr<Gem::XGeneric>>  m_PendingKeepAlives;
};
