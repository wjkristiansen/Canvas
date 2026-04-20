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

    SetD3D12DebugName(m_pCommandQueue, "CopyQueue_CommandQueue");
    SetD3D12DebugName(m_pFence, "CopyQueue_Fence");

    m_AllocatorPool.Init(pDevice, D3D12_COMMAND_LIST_TYPE_COPY);
    m_AllocatorPool.SwapAllocator(m_pAllocator, 0, 0);

    // 1 MB initial — same as the render queue's ring; grows on demand.
    m_UploadRing.Initialize(pDevice, 1 * 1024 * 1024);

    m_TimelineId = pDevice->GetResourceManager().RegisterTimeline(m_pFence);

    m_PendingBuffers.reserve(64);
    m_PendingTextures.reserve(16);
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
    PendingBufferCopy op;
    op.pSrc      = pSrc;          // CComPtr AddRefs; keeps staging alive past ring grow
    op.SrcOffset = srcOffset;
    op.pDst      = pDst;
    op.DstOffset = dstOffset;
    op.Size      = size;
    m_PendingBuffers.push_back(std::move(op));
    if (pDstKeepAlive)
        m_PendingKeepAlives.push_back(std::move(pDstKeepAlive));
}

//------------------------------------------------------------------------------------------------
void CCopyQueue::EnqueueTextureCopy(
    ID3D12Resource* pSrc, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& srcFootprint,
    ID3D12Resource* pDst, uint32_t dstSubresource,
    uint32_t dstX, uint32_t dstY,
    uint32_t width, uint32_t height,
    Gem::TGemPtr<Gem::XGeneric> pDstKeepAlive)
{
    if (!pSrc || !pDst || width == 0 || height == 0)
        return;

    PendingTextureCopy op;
    op.pSrc           = pSrc;     // CComPtr AddRefs; keeps staging alive past ring grow
    op.SrcFootprint   = srcFootprint;
    op.pDst           = pDst;
    op.DstSubresource = dstSubresource;
    op.DstX           = dstX;
    op.DstY           = dstY;
    op.Width          = width;
    op.Height         = height;

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_PendingTextures.push_back(std::move(op));
    if (pDstKeepAlive)
        m_PendingKeepAlives.push_back(std::move(pDstKeepAlive));
}

//------------------------------------------------------------------------------------------------
std::optional<FenceToken> CCopyQueue::FlushIfPending()
{
    std::vector<PendingBufferCopy>           bufferOps;
    std::vector<PendingTextureCopy>          textureOps;
    std::vector<Gem::TGemPtr<Gem::XGeneric>> keepAlives;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_PendingBuffers.empty() && m_PendingTextures.empty())
            return std::nullopt;
        bufferOps  = std::move(m_PendingBuffers);
        textureOps = std::move(m_PendingTextures);
        keepAlives = std::move(m_PendingKeepAlives);
        m_PendingBuffers.clear();
        m_PendingTextures.clear();
        m_PendingKeepAlives.clear();
    }

    const UINT64 completedValue = m_pFence->GetCompletedValue();

    CComPtr<ID3D12GraphicsCommandList> pCL;
    Gem::ThrowGemError(ResultFromHRESULT(m_pDevice->GetD3DDevice()->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_COPY, m_pAllocator, nullptr, IID_PPV_ARGS(&pCL))));
    SetD3D12DebugName(pCL, "CopyQueue_CommandList");

    for (const auto& op : bufferOps)
        pCL->CopyBufferRegion(op.pDst, op.DstOffset, op.pSrc, op.SrcOffset, op.Size);

    for (const auto& op : textureOps)
    {
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource       = op.pSrc;
        src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = op.SrcFootprint;

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource        = op.pDst;
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = op.DstSubresource;

        D3D12_BOX srcBox = { 0, 0, 0, op.Width, op.Height, 1 };
        pCL->CopyTextureRegion(&dst, op.DstX, op.DstY, 0, &src, &srcBox);
    }

    Gem::ThrowGemError(ResultFromHRESULT(pCL->Close()));

    ID3D12CommandList* pLists[] = { pCL };
    m_pCommandQueue->ExecuteCommandLists(1, pLists);

    const UINT64 signalValue = ++m_LastSignaledValue;
    Gem::ThrowGemError(ResultFromHRESULT(m_pCommandQueue->Signal(m_pFence, signalValue)));

    m_UploadRing.MarkSubmissionEnd(signalValue);
    m_UploadRing.Reclaim(completedValue);

    // Defer-release the unique staging source resources until the copy fence
    // reaches signalValue.  The upload ring may release/replace its backing
    // resource between flushes (CUploadRing::GrowTo), so we cannot rely on
    // the ring to keep the source alive for the GPU.
    {
        StagingRetention retention;
        retention.FenceValue = signalValue;
        retention.Resources.reserve(bufferOps.size() + textureOps.size());
        ID3D12Resource* pPrev = nullptr;
        for (auto& op : bufferOps)
        {
            if (op.pSrc.p != pPrev)
            {
                retention.Resources.push_back(op.pSrc);
                pPrev = op.pSrc.p;
            }
        }
        for (auto& op : textureOps)
        {
            if (op.pSrc.p != pPrev)
            {
                retention.Resources.push_back(op.pSrc);
                pPrev = op.pSrc.p;
            }
        }
        m_StagingRetention.push_back(std::move(retention));

        // Reap any retentions whose fence has already completed.
        while (!m_StagingRetention.empty() &&
               m_StagingRetention.front().FenceValue <= completedValue)
        {
            m_StagingRetention.pop_front();
        }
    }

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
