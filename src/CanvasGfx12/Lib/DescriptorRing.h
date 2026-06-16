//================================================================================================
// CDescriptorRing - Fence-protected ring allocator for a fixed-size descriptor heap.
//
// Hands out contiguous runs of descriptor slots from a single D3D12 descriptor heap, treating
// the heap as a ring.  Slots handed out during a frame stay reserved until the GPU work that
// references them has completed, tracked by per-submission {fenceValue, slotCount} markers.
// When the ring cannot satisfy a request without overwriting slots still referenced by
// in-flight GPU work, it blocks on the owning queue's fence and reclaims completed
// submissions before reusing those slots.
//
// This guards against the bug where simple modular wrap-around reuses a descriptor that the
// GPU is still reading from a prior frame.
//
// Slot granularity only: the ring tracks slot indices, not bytes, so the caller maps the
// returned base slot to a CPU/GPU handle using the heap's descriptor increment size.  The
// heap itself is owned and sized by the caller; the ring never creates or resizes it.
//
// NOT thread-safe; intended to be owned by a single CRenderQueue12 and touched only by that
// queue's thread (the same model as CUploadRing).
//================================================================================================

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <Gem.hpp>

//------------------------------------------------------------------------------------------------
class CDescriptorRing
{
public:
    // Invoked when the ring must wait for in-flight GPU work to retire before reusing slots.
    // Implementations block the CPU until the queue fence reaches the supplied value.
    using WaitFn = std::function<void(UINT64)>;

    CDescriptorRing() = default;

    CDescriptorRing(const CDescriptorRing&) = delete;
    CDescriptorRing& operator=(const CDescriptorRing&) = delete;

    // Manage `capacity` contiguous slots starting at `baseSlot`, and reset all bookkeeping.
    // baseSlot is non-zero when the ring shares a heap with a persistent allocator that owns
    // the slots below it; the returned slot indices are absolute (include baseSlot).
    void Initialize(UINT baseSlot, UINT capacity);

    // Drop all markers and rewind the cursor.  Caller must guarantee the GPU is idle first.
    void Reset();

    // Reserve `count` contiguous slots and return the absolute base slot index in
    // [baseSlot, baseSlot + capacity).
    // A descriptor table must be contiguous, so a run never straddles the end of the heap;
    // when the cursor is too close to the end the trailing slack is abandoned and the run
    // restarts at slot 0.  Reclaims completed submissions first, and blocks via waitFn only
    // when live in-flight slots must be freed to make room.  Throws Gem::Result::OutOfMemory
    // if `count` exceeds the heap capacity or if a single un-submitted frame's demand cannot
    // be satisfied (no in-flight work remains to wait on).
    UINT Allocate(UINT count, const WaitFn& waitFn);

    // Record that every slot handed out since the previous mark belongs to the submission
    // signalling `fenceValue`; those slots become reclaimable once that fence completes.
    void MarkSubmissionEnd(UINT64 fenceValue);

    // Release the slots of every submission whose fence value has completed.
    void Reclaim(UINT64 completedFenceValue);

    // Cache the latest known completed fence value for the cheap reclaim attempt that
    // Allocate performs before it considers blocking.
    void SetLastCompletedFenceValue(UINT64 value) { m_LastCompletedFenceValue = value; }

private:
    UINT m_BaseSlot    = 0;   // absolute index of the ring's first slot in the heap
    UINT m_Capacity    = 0;   // descriptor slots managed by the ring
    UINT m_WriteOffset = 0;   // next free slot (head), relative to m_BaseSlot
    UINT m_ReadOffset  = 0;   // oldest in-flight slot (tail), relative to m_BaseSlot
    // Authoritative count of slots reserved by in-flight (un-reclaimed) submissions plus
    // the current un-submitted frame.  Removes the head==tail full-vs-empty ambiguity:
    // free <=> m_Capacity - m_InFlight.
    UINT m_InFlight    = 0;
    // Slots reserved since the most recent MarkSubmissionEnd, folded into the next marker.
    UINT m_AllocatedThisFrame = 0;
    UINT64 m_LastCompletedFenceValue = 0;

    struct FrameMarker
    {
        UINT64 FenceValue;     // value the owning queue signals for this submission
        UINT   SlotsInFrame;   // slots (including abandoned wrap slack) to release on reclaim
    };
    std::deque<FrameMarker> m_FrameMarkers;
};
