#pragma once

#include <atlbase.h>
#include <d3d12.h>
#include <map>

class CDevice12;

class CCommandAllocatorPool
{
    std::multimap<UINT64, CComPtr<ID3D12CommandAllocator>> m_Allocators;
    CComPtr<ID3D12Device> m_pDevice;
    D3D12_COMMAND_LIST_TYPE m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

public:
    CCommandAllocatorPool();

    void Init(CDevice12 *pDevice, D3D12_COMMAND_LIST_TYPE Type);
    void SwapAllocator(CComPtr<ID3D12CommandAllocator> &allocator, UINT64 fenceValue, UINT64 completedFenceValue);
};
