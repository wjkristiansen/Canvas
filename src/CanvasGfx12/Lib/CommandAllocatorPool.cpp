#include "pch.h"
#include "CommandAllocatorPool.h"
#include "Device12.h"

//------------------------------------------------------------------------------------------------
CCommandAllocatorPool::CCommandAllocatorPool()
{
}

//------------------------------------------------------------------------------------------------
void CCommandAllocatorPool::Init(CDevice12 *pDevice, D3D12_COMMAND_LIST_TYPE Type)
{
    m_pDevice = pDevice->GetD3DDevice();
    m_Type = Type;
}

//------------------------------------------------------------------------------------------------
void CCommandAllocatorPool::SwapAllocator(CComPtr<ID3D12CommandAllocator> &allocator, UINT64 fenceValue, UINT64 completedFenceValue)
{
    if (allocator)
        m_Allocators.emplace(fenceValue, std::move(allocator));

    if (!m_Allocators.empty() && m_Allocators.begin()->first <= completedFenceValue)
    {
        auto node = m_Allocators.extract(m_Allocators.begin());
        allocator = std::move(node.mapped());
    }
    else
    {
        m_pDevice->CreateCommandAllocator(m_Type, IID_PPV_ARGS(&allocator));
    }
}

