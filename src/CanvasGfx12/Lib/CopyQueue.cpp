//================================================================================================
// CCopyQueue - Implementation
//================================================================================================

#include "pch.h"
#include "CopyQueue.h"
#include "Device12.h"

//------------------------------------------------------------------------------------------------
void CCopyQueue::Initialize(CDevice12* pDevice)
{
    m_pDevice = pDevice;
    auto* pD3DDevice = pDevice->GetD3DDevice();

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type     = D3D12_COMMAND_LIST_TYPE_COPY;
    desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 1;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    Gem::ThrowGemError(ResultFromHRESULT(
        pD3DDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_pCommandQueue))));

    Gem::ThrowGemError(ResultFromHRESULT(
        pD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence))));

    m_AllocatorPool.Init(pDevice, D3D12_COMMAND_LIST_TYPE_COPY);
    m_AllocatorPool.SwapAllocator(m_pAllocator, 0, 0);

    // 1 MB initial — same as the render queue's ring; grows on demand.
    m_UploadRing.Initialize(pDevice, 1 * 1024 * 1024);

    m_TimelineId = pDevice->GetResourceManager().RegisterTimeline(m_pFence);

    m_Pending.reserve(64);
    m_PendingKeepAlives.reserve(64);
}

//------------------------------------------------------------------------------------------------
void CCopyQueue::Shutdown()
{
    if (!m_pCommandQueue)
        return;

    DrainGpu();

    if (m_TimelineId != FenceToken::kInvalidTimelineId)
    {
        m_pDevice->GetResourceManager().UnregisterTimeline(m_TimelineId);
        m_TimelineId = FenceToken::kInvalidTimelineId;
    }

    m_UploadRing.Shutdown();
    m_pAllocator.Release();
    m_pFence.Release();
    m_pCommandQueue.Release();
    m_pDevice = nullptr;
}

//------------------------------------------------------------------------------------------------
void CCopyQueue::DrainGpu()
{
    if (!m_pFence)
        return;

    if (m_pFence->GetCompletedValue() < m_LastSignaledValue)
    {
        HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (hEvent)
        {
            m_pFence->SetEventOnCompletion(m_LastSignaledValue, hEvent);
            WaitForSingleObject(hEvent, INFINITE);
            CloseHandle(hEvent);
        }
    }
}

//------------------------------------------------------------------------------------------------
void CCopyQueue::EnqueueBufferCopy(
    ID3D12Resource* pSrc, uint64_t srcOffset,
    ID3D12Resource* pDst, uint64_t dstOffset,
    uint64_t size,
    Gem::TGemPtr<Gem::XGeneric> pDstKeepAlive)
{
    if (!pSrc || !pDst || size == 0)
        return;

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Pending.push_back({ pSrc, srcOffset, pDst, dstOffset, size });
    if (pDstKeepAlive)
        m_PendingKeepAlives.push_back(std::move(pDstKeepAlive));
}

//------------------------------------------------------------------------------------------------
std::optional<FenceToken> CCopyQueue::FlushIfPending()
{
    std::vector<PendingCopy>                 pending;
    std::vector<Gem::TGemPtr<Gem::XGeneric>> keepAlives;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Pending.empty())
            return std::nullopt;
        pending    = std::move(m_Pending);
        keepAlives = std::move(m_PendingKeepAlives);
        m_Pending.clear();
        m_PendingKeepAlives.clear();
    }

    const UINT64 completedValue = m_pFence->GetCompletedValue();

    CComPtr<ID3D12GraphicsCommandList> pCL;
    Gem::ThrowGemError(ResultFromHRESULT(m_pDevice->GetD3DDevice()->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_COPY, m_pAllocator, nullptr, IID_PPV_ARGS(&pCL))));

    for (const auto& op : pending)
        pCL->CopyBufferRegion(op.pDst, op.DstOffset, op.pSrc, op.SrcOffset, op.Size);

    Gem::ThrowGemError(ResultFromHRESULT(pCL->Close()));

    ID3D12CommandList* pLists[] = { pCL };
    m_pCommandQueue->ExecuteCommandLists(1, pLists);

    const UINT64 signalValue = ++m_LastSignaledValue;
    Gem::ThrowGemError(ResultFromHRESULT(m_pCommandQueue->Signal(m_pFence, signalValue)));

    m_UploadRing.MarkSubmissionEnd(signalValue);
    m_UploadRing.Reclaim(completedValue);

    // Recycle the allocator: put the just-used one back keyed to signalValue,
    // pick up the next one (or a freshly created one) and reset it for next flush.
    m_AllocatorPool.SwapAllocator(m_pAllocator, signalValue, completedValue);
    Gem::ThrowGemError(ResultFromHRESULT(m_pAllocator->Reset()));

    const FenceToken token{ m_TimelineId, signalValue };
    auto& mgr = m_pDevice->GetResourceManager();
    for (auto& p : keepAlives)
        mgr.DeferRelease(std::move(p), token);

    return token;
}
