//================================================================================================
// CDescriptorHeapAllocator - Persistent allocator over a descriptor-heap slot range.
//
// Hands out contiguous runs of descriptor slots that live until explicitly freed, i.e. for the
// lifetime of the resource that owns them (a mesh's vertex-stream SRVs, a material's texture
// SRVs).  This is the counterpart to CDescriptorRing: the ring recycles slots every frame for
// transient per-draw descriptors, while this allocator holds slots indefinitely so per-resource
// descriptors can be created once and bound by GPU handle every draw with no per-frame cost.
//
// The two share one shader-visible heap by partitioning it: this allocator owns a low slot
// range [baseSlot, baseSlot + count); the ring owns the remainder.
//
// Implemented as size-segregated free lists plus a bump pointer.  The handful of block sizes in
// use are fixed per resource type (e.g. 5 for a mesh group, 6 for a material), so a free list
// per size gives O(1) allocate/free with exact-fit reuse: no scan and no rounding waste (unlike
// a buddy allocator, which would round 5 and 6 up to 8).  A freed block is reusable only by a
// same-size request, which matches how each resource type churns within its own size class.
//
// Thread-safe: resources may be created and destroyed from application threads, so Allocate /
// Free take an internal lock.
//================================================================================================

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

//------------------------------------------------------------------------------------------------
class CDescriptorHeapAllocator
{
public:
    static constexpr UINT kInvalidSlot = UINT(-1);

    CDescriptorHeapAllocator() = default;

    CDescriptorHeapAllocator(const CDescriptorHeapAllocator&) = delete;
    CDescriptorHeapAllocator& operator=(const CDescriptorHeapAllocator&) = delete;

    // Manage the slot range [baseSlot, baseSlot + count).  Resets all bookkeeping.
    void Initialize(UINT baseSlot, UINT count);

    // Allocate `count` contiguous slots; returns the absolute base slot index, or kInvalidSlot
    // when the managed range is exhausted (no same-size free block and the bump region is full).
    UINT Allocate(UINT count);

    // Return a run previously handed out by Allocate so a later same-size Allocate can reuse it.
    // `count` must match the value passed to the originating Allocate.
    void Free(UINT baseSlot, UINT count);

private:
    UINT m_Next = 0;   // next never-used slot (bump pointer)
    UINT m_End  = 0;   // one past the last managed slot

    // size (in slots) -> stack of free block base slots of that exact size.
    std::unordered_map<UINT, std::vector<UINT>> m_FreeLists;

    std::mutex m_Mutex;
};
