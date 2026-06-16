//================================================================================================
// CDescriptorRing - Implementation.
//================================================================================================

#include "pch.h"
#include "DescriptorRing.h"

//------------------------------------------------------------------------------------------------
void CDescriptorRing::Initialize(UINT baseSlot, UINT capacity)
{
    m_BaseSlot            = baseSlot;
    m_Capacity            = capacity;
    m_WriteOffset         = 0;
    m_ReadOffset          = 0;
    m_InFlight            = 0;
    m_AllocatedThisFrame  = 0;
    m_LastCompletedFenceValue = 0;
    m_FrameMarkers.clear();
}

//------------------------------------------------------------------------------------------------
void CDescriptorRing::Reset()
{
    m_WriteOffset        = 0;
    m_ReadOffset         = 0;
    m_InFlight           = 0;
    m_AllocatedThisFrame = 0;
    m_FrameMarkers.clear();
}

//------------------------------------------------------------------------------------------------
UINT CDescriptorRing::Allocate(UINT count, const WaitFn& waitFn)
{
    // A run larger than the whole heap can never be satisfied.
    if (count == 0 || count > m_Capacity)
        throw Gem::GemError(Gem::Result::OutOfMemory);

    auto available = [&]() -> UINT { return m_Capacity - m_InFlight; };

    // Block on (and then reclaim) the oldest in-flight submission.  Caller must ensure a
    // marker exists; the only way to run dry of markers is a single un-submitted frame
    // demanding more than the heap holds, which the callers below report as OutOfMemory.
    auto reclaimOldestBlocking = [&]()
    {
        const UINT64 fenceValue = m_FrameMarkers.front().FenceValue;
        waitFn(fenceValue);
        Reclaim(fenceValue);
    };

    // Reclaim anything the GPU has already finished, for free.
    Reclaim(m_LastCompletedFenceValue);

    // When the ring has drained completely, restart the cursor at slot 0.  Nothing
    // references any slot, so this cannot overwrite live work and it keeps long runs from
    // accumulating abandoned wrap slack.
    if (m_InFlight == 0)
    {
        m_WriteOffset = 0;
        m_ReadOffset  = 0;
    }

    // Ensure enough total free slots, blocking on in-flight submissions only when the cheap
    // reclaim above was not enough.
    while (count > available())
    {
        if (m_FrameMarkers.empty())
            throw Gem::GemError(Gem::Result::OutOfMemory);  // one frame needs more than the heap
        reclaimOldestBlocking();
    }

    // A descriptor table must be contiguous, so a run cannot straddle the end of the heap.
    // When it would, abandon the slack from the cursor to the end and restart at slot 0 --
    // but first make sure slots [0, count) are clear of in-flight descriptors.
    UINT wrapSlack = 0;
    if (m_WriteOffset + count > m_Capacity)
    {
        while (count > m_ReadOffset && m_InFlight > 0)
            reclaimOldestBlocking();

        if (m_InFlight == 0)
        {
            // The ring drained while making room; lay out fresh from the start with no
            // slack to charge.
            m_WriteOffset = 0;
            m_ReadOffset  = 0;
        }
        else
        {
            wrapSlack     = m_Capacity - m_WriteOffset;
            m_WriteOffset = 0;
        }
    }

    const UINT baseSlot = m_WriteOffset;
    m_WriteOffset += count;

    const UINT consumed   = count + wrapSlack;
    m_InFlight           += consumed;
    m_AllocatedThisFrame += consumed;
    // Hand back an absolute heap slot; internal bookkeeping stays relative to m_BaseSlot.
    return m_BaseSlot + baseSlot;
}

//------------------------------------------------------------------------------------------------
void CDescriptorRing::MarkSubmissionEnd(UINT64 fenceValue)
{
    // Frames that reserved no slots add no marker; the cursor is unchanged so there is
    // nothing to reclaim later.
    if (m_AllocatedThisFrame == 0)
        return;

    m_FrameMarkers.push_back({ fenceValue, m_AllocatedThisFrame });
    m_AllocatedThisFrame = 0;
}

//------------------------------------------------------------------------------------------------
void CDescriptorRing::Reclaim(UINT64 completedFenceValue)
{
    m_LastCompletedFenceValue = completedFenceValue;

    // Advance the read pointer past each fully retired submission, shrinking the in-flight
    // count by exactly that submission's slot footprint (including any abandoned wrap slack)
    // so the read pointer tracks the write pointer monotonically through the ring.
    while (!m_FrameMarkers.empty())
    {
        const FrameMarker& oldest = m_FrameMarkers.front();
        if (oldest.FenceValue > completedFenceValue)
            break;
        m_InFlight  -= oldest.SlotsInFrame;
        m_ReadOffset = (m_ReadOffset + oldest.SlotsInFrame) % m_Capacity;
        m_FrameMarkers.pop_front();
    }
}
