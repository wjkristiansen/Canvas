//================================================================================================
// CDescriptorHeapAllocator - Implementation.
//================================================================================================

#include "pch.h"
#include "DescriptorHeapAllocator.h"

//------------------------------------------------------------------------------------------------
void CDescriptorHeapAllocator::Initialize(UINT baseSlot, UINT count)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Next = baseSlot;
    m_End  = baseSlot + count;
    m_FreeLists.clear();
}

//------------------------------------------------------------------------------------------------
UINT CDescriptorHeapAllocator::Allocate(UINT count)
{
    if (count == 0)
        return kInvalidSlot;

    std::lock_guard<std::mutex> lock(m_Mutex);

    // Reuse a previously-freed block of this exact size if one is available.
    auto it = m_FreeLists.find(count);
    if (it != m_FreeLists.end() && !it->second.empty())
    {
        const UINT base = it->second.back();
        it->second.pop_back();
        return base;
    }

    // Otherwise carve fresh slots off the bump pointer.  Subtraction avoids overflow since
    // m_Next never exceeds m_End.
    if (m_End - m_Next >= count)
    {
        const UINT base = m_Next;
        m_Next += count;
        return base;
    }

    return kInvalidSlot;  // region exhausted
}

//------------------------------------------------------------------------------------------------
void CDescriptorHeapAllocator::Free(UINT baseSlot, UINT count)
{
    if (count == 0 || baseSlot == kInvalidSlot)
        return;

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_FreeLists[count].push_back(baseSlot);
}
