//================================================================================================
// CResourceAllocator - Placed / committed resource allocator for D3D12
//
// Small and medium allocations are placed into shared ID3D12Heaps managed by buddy allocators.
// Large allocations fall back to committed resources.
//================================================================================================

#pragma once

#include "BuddySuballocator.h"
#include <d3d12.h>
#include <atlbase.h>
#include <vector>
#include <memory>

class CDevice12;

//------------------------------------------------------------------------------------------------
struct ResourceAllocation
{
    CComPtr<ID3D12Resource> pResource;
    uint64_t AllocationKey = 0;   // 0 = committed, nonzero = encoded buddy info
    uint32_t SizeInUnits   = 0;   // For buddy block reconstruction on free
    uint32_t AllocatorTier = 0;   // 0 = committed, 1 = small, 2 = large

    // AllocationKey encoding: [sizeInUnits:24 | tier:8 | blockStart+1:32]
    static uint64_t EncodeKey(uint32_t blockStart, uint32_t tier, uint32_t sizeInUnits)
    {
        return (static_cast<uint64_t>(sizeInUnits) << 40)
             | (static_cast<uint64_t>(tier) << 32)
             | static_cast<uint64_t>(blockStart + 1);
    }

    static void DecodeKey(uint64_t key, uint32_t& blockStart, uint32_t& tier, uint32_t& sizeInUnits)
    {
        blockStart  = static_cast<uint32_t>(key & 0xFFFFFFFF) - 1;
        tier        = static_cast<uint32_t>((key >> 32) & 0xFF);
        sizeInUnits = static_cast<uint32_t>((key >> 40) & 0xFFFFFF);
    }
};

//------------------------------------------------------------------------------------------------
class CResourceAllocator
{
public:
    ~CResourceAllocator();

    void Initialize(CDevice12* pDevice);

    Gem::Result Alloc(
        const D3D12_RESOURCE_DESC& desc,
        D3D12_BARRIER_LAYOUT initialLayout,
        ResourceAllocation& out,
        const char* name = nullptr);

    void Free(ResourceAllocation& allocation);

private:
    struct HeapPool
    {
        std::unique_ptr<TBuddySuballocator<uint32_t>> Allocator;
        uint32_t UnitSize            = 0;
        uint32_t HeapCapacityInUnits = 0;
        uint64_t HeapSizeInBytes     = 0;
        std::vector<CComPtr<ID3D12Heap>> Heaps;
        std::vector<CComPtr<ID3D12Heap>> FreePool;
        static constexpr uint32_t kFreePoolCap = 8;
    };

    void EnsureHeap(HeapPool& pool, uint32_t heapIndex);
    void TryRecycleHeap(HeapPool& pool, uint32_t heapIndex);

    Gem::Result AllocPlaced(
        HeapPool& pool,
        const D3D12_RESOURCE_DESC& desc,
        D3D12_RESOURCE_ALLOCATION_INFO allocInfo,
        ResourceAllocation& out,
        const char* name);

    Gem::Result AllocCommitted(
        const D3D12_RESOURCE_DESC& desc,
        ResourceAllocation& out,
        const char* name);

    void SetResourceName(ID3D12Resource* pResource, const char* name);

    HeapPool  m_SmallPool;
    HeapPool  m_LargePool;
    bool      m_TightAlignmentSupported = false;
    CDevice12* m_pDevice = nullptr;
};
