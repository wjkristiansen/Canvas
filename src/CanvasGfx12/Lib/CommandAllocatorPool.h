#pragma once

#include <atlbase.h>
#include <d3d12.h>
#include <vector>

class CDevice12;
class CRenderQueue12;

class CCommandAllocatorPool
{
    struct AllocatorElement
    {
        CComPtr<ID3D12CommandAllocator> pCommandAllocator;
        UINT64 FenceValue;
    };

    std::vector<AllocatorElement> CommandAllocators;
    UINT AllocatorIndex = 0;

public:
    CCommandAllocatorPool();

    ID3D12CommandAllocator *Init(CDevice12 *pDevice, D3D12_COMMAND_LIST_TYPE Type, UINT NumAllocators);
    ID3D12CommandAllocator *RotateAllocators(CRenderQueue12 *pRenderQueue);
};
