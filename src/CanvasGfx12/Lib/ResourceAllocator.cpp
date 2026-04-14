//================================================================================================
// CResourceAllocator implementation
//================================================================================================

#include "pch.h"
#include "ResourceAllocator.h"
#include "Device12.h"

//------------------------------------------------------------------------------------------------
CResourceAllocator::~CResourceAllocator()
{
    auto checkLeaks = [this](const HeapPool& pool, const char* name)
    {
        for (uint32_t i = 0; i < pool.Heaps.size(); ++i)
        {
            if (!pool.Heaps[i])
                continue;
            uint32_t heapStart = i * pool.HeapCapacityInUnits;
            auto heapBlock = TBuddyBlock<uint32_t>(heapStart,
                static_cast<uint8_t>(Log2Ceil(pool.HeapCapacityInUnits)));
            if (!pool.Allocator->IsBlockFree(heapBlock))
            {
                if (m_pDevice)
                    Canvas::LogError(m_pDevice->GetLogger(),
                        "CResourceAllocator: leaked allocations in %s pool heap %u", name, i);
            }
        }
    };

    checkLeaks(m_SmallPool, "small");
    checkLeaks(m_LargePool, "large");
}

//------------------------------------------------------------------------------------------------
void CResourceAllocator::Initialize(CDevice12* pDevice)
{
    m_pDevice = pDevice;

    // Query tight alignment support
#ifndef D3D12_FEATURE_TIGHT_ALIGNMENT
    m_TightAlignmentSupported = false;
#else
    D3D12_FEATURE_DATA_TIGHT_ALIGNMENT tightData = {};
    HRESULT hr = pDevice->GetD3DDevice()->CheckFeatureSupport(
        D3D12_FEATURE_TIGHT_ALIGNMENT, &tightData, sizeof(tightData));
    m_TightAlignmentSupported = SUCCEEDED(hr) &&
        tightData.SupportTier >= D3D12_TIGHT_ALIGNMENT_TIER_1;
#endif

    // Small pool: 256B units / 512KB heaps when tight, else 64KB units / 4MB heaps
    if (m_TightAlignmentSupported)
    {
        m_SmallPool.UnitSize        = 256;
        m_SmallPool.HeapSizeInBytes = 512 * 1024;
    }
    else
    {
        m_SmallPool.UnitSize        = 65536;
        m_SmallPool.HeapSizeInBytes = 4 * 1024 * 1024;
    }
    m_SmallPool.HeapCapacityInUnits = static_cast<uint32_t>(
        m_SmallPool.HeapSizeInBytes / m_SmallPool.UnitSize);

    // Large pool: 64KB units / 64MB heaps
    m_LargePool.UnitSize            = 65536;
    m_LargePool.HeapSizeInBytes     = 64 * 1024 * 1024;
    m_LargePool.HeapCapacityInUnits = static_cast<uint32_t>(
        m_LargePool.HeapSizeInBytes / m_LargePool.UnitSize);

    // Initialize buddy allocators with one heap's worth of capacity
    m_SmallPool.Allocator = std::make_unique<TBuddySuballocator<uint32_t>>(m_SmallPool.HeapCapacityInUnits);
    m_LargePool.Allocator = std::make_unique<TBuddySuballocator<uint32_t>>(m_LargePool.HeapCapacityInUnits);

    Canvas::LogDebug(m_pDevice->GetLogger(),
        "CResourceAllocator: initialized (tight=%s, small unit=%uB, large unit=%uB)",
        m_TightAlignmentSupported ? "yes" : "no",
        m_SmallPool.UnitSize, m_LargePool.UnitSize);
}

//------------------------------------------------------------------------------------------------
Gem::Result CResourceAllocator::Alloc(
    const D3D12_RESOURCE_DESC& desc,
    D3D12_BARRIER_LAYOUT /*initialLayout*/,
    ResourceAllocation& out,
    const char* name)
{
    try
    {
        D3D12_RESOURCE_ALLOCATION_INFO allocInfo =
            m_pDevice->GetD3DDevice()->GetResourceAllocationInfo(0, 1, &desc);

        uint64_t size      = allocInfo.SizeInBytes;
        uint64_t alignment = allocInfo.Alignment;

        // Route: small pool → large pool → committed
        if (size <= m_SmallPool.HeapSizeInBytes && alignment <= m_SmallPool.UnitSize)
        {
            out.AllocatorTier = 1;
            return AllocPlaced(m_SmallPool, desc, allocInfo, out, name);
        }
        else if (size <= m_LargePool.HeapSizeInBytes)
        {
            out.AllocatorTier = 2;
            return AllocPlaced(m_LargePool, desc, allocInfo, out, name);
        }
        else
        {
            out.AllocatorTier = 0;
            return AllocCommitted(desc, out, name);
        }
    }
    catch (_com_error& e) { return ResultFromHRESULT(e.Error()); }
    catch (Gem::GemError& e) { return e.Result(); }
}

//------------------------------------------------------------------------------------------------
Gem::Result CResourceAllocator::AllocPlaced(
    HeapPool& pool,
    const D3D12_RESOURCE_DESC& desc,
    D3D12_RESOURCE_ALLOCATION_INFO allocInfo,
    ResourceAllocation& out,
    const char* name)
{
    uint32_t sizeInUnits = static_cast<uint32_t>(
        (allocInfo.SizeInBytes + pool.UnitSize - 1) / pool.UnitSize);

    TBuddyBlock<uint32_t> block;
    if (!pool.Allocator->TryAllocate(sizeInUnits, block))
    {
        pool.Allocator->Grow();
        if (!pool.Allocator->TryAllocate(sizeInUnits, block))
            return Gem::Result::OutOfMemory;
    }

    uint32_t heapIndex    = block.Start() / pool.HeapCapacityInUnits;
    uint64_t offsetInHeap = static_cast<uint64_t>(block.Start() % pool.HeapCapacityInUnits)
                            * pool.UnitSize;
    EnsureHeap(pool, heapIndex);

    D3D12_RESOURCE_DESC localDesc = desc;
#ifdef D3D12_RESOURCE_FLAG_USE_TIGHT_ALIGNMENT
    if (&pool == &m_SmallPool && m_TightAlignmentSupported)
    {
        localDesc.Flags |= D3D12_RESOURCE_FLAG_USE_TIGHT_ALIGNMENT;
        localDesc.Alignment = pool.UnitSize;
    }
#endif

    CComPtr<ID3D12Resource> pResource;
    ThrowFailedHResult(m_pDevice->GetD3DDevice()->CreatePlacedResource(
        pool.Heaps[heapIndex],
        offsetInHeap,
        &localDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&pResource)));

    uint32_t tier = (&pool == &m_SmallPool) ? 1u : 2u;
    out.pResource     = pResource;
    out.AllocationKey = ResourceAllocation::EncodeKey(block.Start(), tier, sizeInUnits);
    out.SizeInUnits   = sizeInUnits;
    out.AllocatorTier = tier;

    SetResourceName(pResource, name);

    Canvas::LogDebug(m_pDevice->GetLogger(),
        "CResourceAllocator: Alloc \"%s\" %lluB -> %s pool, heap %u, offset %llu, block [%u,%u]",
        name ? name : "(unnamed)",
        static_cast<unsigned long long>(allocInfo.SizeInBytes),
        (&pool == &m_SmallPool) ? "small" : "large",
        heapIndex,
        static_cast<unsigned long long>(offsetInHeap),
        block.Start(),
        static_cast<uint32_t>(block.Size()));

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CResourceAllocator::AllocCommitted(
    const D3D12_RESOURCE_DESC& desc,
    ResourceAllocation& out,
    const char* name)
{
    D3D12_RESOURCE_DESC1 desc1 = {};
    desc1.Dimension          = desc.Dimension;
    desc1.Alignment          = desc.Alignment;
    desc1.Width              = desc.Width;
    desc1.Height             = desc.Height;
    desc1.DepthOrArraySize   = desc.DepthOrArraySize;
    desc1.MipLevels          = desc.MipLevels;
    desc1.Format             = desc.Format;
    desc1.SampleDesc         = desc.SampleDesc;
    desc1.Layout             = desc.Layout;
    desc1.Flags              = desc.Flags;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    CComPtr<ID3D12Resource> pResource;
    ThrowFailedHResult(m_pDevice->GetD3DDevice()->CreateCommittedResource3(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc1,
        D3D12_BARRIER_LAYOUT_UNDEFINED,
        nullptr, nullptr, 0, nullptr,
        IID_PPV_ARGS(&pResource)));

    out.pResource     = pResource;
    out.AllocationKey = 0;
    out.SizeInUnits   = 0;
    out.AllocatorTier = 0;

    SetResourceName(pResource, name);

    Canvas::LogDebug(m_pDevice->GetLogger(),
        "CResourceAllocator: Alloc \"%s\" -> committed",
        name ? name : "(unnamed)");

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CResourceAllocator::Free(ResourceAllocation& allocation)
{
    allocation.pResource = nullptr;

    if (allocation.AllocationKey == 0)
        return;     // Committed — heap freed implicitly with resource

    uint32_t blockStart, tier, sizeInUnits;
    ResourceAllocation::DecodeKey(allocation.AllocationKey, blockStart, tier, sizeInUnits);

    HeapPool& pool = (tier == 1) ? m_SmallPool : m_LargePool;

    auto block = TBuddySuballocator<uint32_t>::ReconstructBlock(blockStart, sizeInUnits);
    pool.Allocator->Free(block);

    uint32_t heapIndex = blockStart / pool.HeapCapacityInUnits;

    Canvas::LogDebug(m_pDevice->GetLogger(),
        "CResourceAllocator: Free %s pool, heap %u, block [%u,%u]",
        (tier == 1) ? "small" : "large",
        heapIndex, blockStart, sizeInUnits);

    TryRecycleHeap(pool, heapIndex);

    allocation.AllocationKey = 0;
    allocation.SizeInUnits   = 0;
    allocation.AllocatorTier = 0;
}

//------------------------------------------------------------------------------------------------
void CResourceAllocator::EnsureHeap(HeapPool& pool, uint32_t heapIndex)
{
    if (heapIndex < pool.Heaps.size() && pool.Heaps[heapIndex])
        return;
    if (heapIndex >= pool.Heaps.size())
        pool.Heaps.resize(heapIndex + 1);

    CComPtr<ID3D12Heap> pHeap;
    if (!pool.FreePool.empty())
    {
        pHeap = std::move(pool.FreePool.back());
        pool.FreePool.pop_back();
    }
    else
    {
        D3D12_HEAP_DESC heapDesc = {};
        heapDesc.SizeInBytes     = pool.HeapSizeInBytes;
        heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapDesc.Alignment       = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        heapDesc.Flags           = D3D12_HEAP_FLAG_NONE;

        ThrowFailedHResult(m_pDevice->GetD3DDevice()->CreateHeap(
            &heapDesc, IID_PPV_ARGS(&pHeap)));
    }
    pool.Heaps[heapIndex] = pHeap;
}

//------------------------------------------------------------------------------------------------
void CResourceAllocator::TryRecycleHeap(HeapPool& pool, uint32_t heapIndex)
{
    if (heapIndex >= pool.Heaps.size() || !pool.Heaps[heapIndex])
        return;

    uint32_t heapStart = heapIndex * pool.HeapCapacityInUnits;
    uint8_t  heapOrder = static_cast<uint8_t>(Log2Ceil(pool.HeapCapacityInUnits));
    TBuddyBlock<uint32_t> heapBlock(heapStart, heapOrder);

    if (!pool.Allocator->IsBlockFree(heapBlock))
        return;

    if (pool.FreePool.size() < HeapPool::kFreePoolCap)
        pool.FreePool.push_back(std::move(pool.Heaps[heapIndex]));
    pool.Heaps[heapIndex] = nullptr;
}

//------------------------------------------------------------------------------------------------
void CResourceAllocator::SetResourceName(ID3D12Resource* pResource, const char* name)
{
    if (!name || !pResource)
        return;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    if (wlen > 0)
    {
        std::wstring wname(wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name, -1, wname.data(), wlen);
        pResource->SetName(wname.c_str());
    }
}
