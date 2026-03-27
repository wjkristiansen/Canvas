#include "pch.h"
#include "CommandAllocatorPool.h"
#include "Device12.h"
#include "RenderQueue12.h"

//------------------------------------------------------------------------------------------------
CCommandAllocatorPool::CCommandAllocatorPool()
{
}

//------------------------------------------------------------------------------------------------
ID3D12CommandAllocator *CCommandAllocatorPool::Init(CDevice12 *pDevice, D3D12_COMMAND_LIST_TYPE Type, UINT NumAllocators)
{
    Canvas::CFunctionSentinel sentinel("CCommandAllocatorPool::Init", pDevice->GetLogger());
    
    for (UINT i = 0; i < NumAllocators; ++i)
    {
        CComPtr<ID3D12CommandAllocator> pAllocator;
        ID3D12Device *pD3DDevice = pDevice->GetD3DDevice();
        pD3DDevice->CreateCommandAllocator(Type, IID_PPV_ARGS(&pAllocator));
        CommandAllocators.push_back({ pAllocator, 0 });
    }

    AllocatorIndex = 0;
    return CommandAllocators[0].pCommandAllocator;
}

//------------------------------------------------------------------------------------------------
ID3D12CommandAllocator *CCommandAllocatorPool::RotateAllocators(CRenderQueue12 *pRenderQueue)
{
    if (pRenderQueue)
        CommandAllocators[AllocatorIndex].FenceValue = pRenderQueue->m_FenceValue;
    AllocatorIndex = (AllocatorIndex + 1) % CommandAllocators.size();

    if (pRenderQueue && CommandAllocators[AllocatorIndex].FenceValue > pRenderQueue->m_pFence->GetCompletedValue())
    {
        HANDLE hEvent = CreateEvent(nullptr, 0, 0, nullptr);
        pRenderQueue->m_pFence->SetEventOnCompletion(CommandAllocators[AllocatorIndex].FenceValue, hEvent);
        WaitForSingleObject(hEvent, INFINITE);
        CloseHandle(hEvent);
    }

    return CommandAllocators[AllocatorIndex].pCommandAllocator;
}
