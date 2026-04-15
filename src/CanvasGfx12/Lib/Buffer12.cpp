//================================================================================================
// Buffer12
//================================================================================================

#include "pch.h"
#include "Buffer12.h"
#include "Device12.h"

CBuffer12::~CBuffer12()
{
    if (m_pDevice && m_AllocationKey != 0)
    {
        ResourceAllocation alloc;
        alloc.AllocationKey = m_AllocationKey;
        alloc.SizeInUnits   = m_SizeInUnits;
        alloc.AllocatorTier = m_AllocatorTier;
        m_pDevice->m_ResourceAllocator.Free(alloc);
    }
}
