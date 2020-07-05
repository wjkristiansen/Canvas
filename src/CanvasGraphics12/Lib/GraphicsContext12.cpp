//================================================================================================
// Context
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CGraphicsContext::CGraphicsContext(CDevice *pDevice) :
    m_pDevice(pDevice)
{
    CComPtr<ID3D12CommandQueue> pCQ;
    D3D12_COMMAND_QUEUE_DESC CQDesc;
    CQDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    CQDesc.NodeMask = 1;
    CQDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    CQDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    auto *pD3DDevice = pDevice->GetD3DDevice();

    ThrowGemError(GemResult(pD3DDevice->CreateCommandQueue(&CQDesc, IID_PPV_ARGS(&pCQ))));

    CComPtr<ID3D12CommandAllocator> pCA;
    ThrowGemError(GemResult(pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCA))));

    CComPtr<ID3D12GraphicsCommandList> pCL;
    ThrowGemError(GemResult(pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pCA, nullptr, IID_PPV_ARGS(&pCL))));

    CComPtr<ID3D12DescriptorHeap> pResDH;
    D3D12_DESCRIPTOR_HEAP_DESC DHDesc = {};
    DHDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    DHDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    DHDesc.NumDescriptors = NumShaderResourceDescriptors; // BUGBUG: This needs to be a well-known constant
    ThrowGemError(GemResult(pD3DDevice->CreateDescriptorHeap(&DHDesc, IID_PPV_ARGS(&pResDH))));

    CComPtr<ID3D12DescriptorHeap> pSamplerDH;
    DHDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    DHDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    DHDesc.NumDescriptors = NumSamplerDescriptors; // BUGBUG: This needs to be a well-known constant
    ThrowGemError(GemResult(pD3DDevice->CreateDescriptorHeap(&DHDesc, IID_PPV_ARGS(&pSamplerDH))));

    CComPtr<ID3D12DescriptorHeap> pRTVDH;
    DHDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    DHDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    DHDesc.NumDescriptors = NumRTVDescriptors; // BUGBUG: This needs to be a well-known constant
    ThrowGemError(GemResult(pD3DDevice->CreateDescriptorHeap(&DHDesc, IID_PPV_ARGS(&pRTVDH))));

    CComPtr<ID3D12DescriptorHeap> pDSVDH;
    DHDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    DHDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    DHDesc.NumDescriptors = NumDSVDescriptors; // BUGBUG: This needs to be a well-known constant
    ThrowGemError(GemResult(pD3DDevice->CreateDescriptorHeap(&DHDesc, IID_PPV_ARGS(&pDSVDH))));

    CComPtr<ID3D12Fence> pFence;
    ThrowGemError(GemResult(pD3DDevice->CreateFence(m_FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence))));

    // The default root signature uses the following parameters
    //  Root CBV (descriptor static)
    //  Root SRV (descriptor static)
    //  Root UAV (descriptor static)
    //  Root descriptor table

    // The default root descriptor table is layed out as follows:
    //  CBV[2] (data static)
    //  SRV[4] (data static)
    //  UAV[2] (descriptor static)

    std::vector<CD3DX12_DESCRIPTOR_RANGE1> DefaultDescriptorRanges(3);
    DefaultDescriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0);
    DefaultDescriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 2);
    DefaultDescriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 6);

    std::vector<CD3DX12_ROOT_PARAMETER1> DefaultRootParams(4);
    DefaultRootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    DefaultRootParams[1].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    DefaultRootParams[2].InitAsUnorderedAccessView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);
    DefaultRootParams[3].InitAsDescriptorTable(1, DefaultDescriptorRanges.data(), D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC DefaultRootSigDesc(4U, DefaultRootParams.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    CComPtr<ID3DBlob> pRSBlob;
    ThrowFailedHResult(D3D12SerializeVersionedRootSignature(&DefaultRootSigDesc, &pRSBlob, nullptr));

    CComPtr<ID3D12RootSignature> pDefaultRootSig;
    pD3DDevice->CreateRootSignature(1, pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(), IID_PPV_ARGS(&pDefaultRootSig));

    m_pShaderResourceDescriptorHeap.Attach(pResDH.Detach());
    m_pSamplerDescriptorHeap.Attach(pSamplerDH.Detach());
    m_pRTVDescriptorHeap.Attach(pRTVDH.Detach());
    m_pDSVDescriptorHeap.Attach(pDSVDH.Detach());
    m_pCommandAllocator.Attach(pCA.Detach());
    m_pCommandList.Attach(pCL.Detach());
    m_pCommandQueue.Attach(pCQ.Detach());
    m_pFence.Attach(pFence.Detach());
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CGraphicsContext::CreateSwapChain(HWND hWnd, bool Windowed, XCanvasGfxSwapChain **ppSwapChain, GfxFormat Format, UINT NumBuffers)
{
    CFunctionSentinel Sentinel(g_Logger, "XCanvasGfxGraphicsContext::CreateSwapChain");
    try
    {
        // Create the swapchain
        DXGI_FORMAT dxgiFormat = CanvasFormatToDXGIFormat(Format);
        TGemPtr<CSwapChain> pSwapChain = new TGeneric<CSwapChain>(hWnd, Windowed, this, dxgiFormat, NumBuffers);
        return pSwapChain->QueryInterface(ppSwapChain);
    }
    catch (GemError &e)
    {
        Sentinel.ReportError(e.Result());
        return e.Result();
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CGraphicsContext::CopyBuffer(XCanvasGfxBuffer *pDest, XCanvasGfxBuffer *pSource)
{
    std::unique_lock<std::mutex> Lock(m_mutex);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CGraphicsContext::ClearSurface(XCanvasGfxSurface *pGfxSurface, const float Color[4])
{
    std::unique_lock<std::mutex> Lock(m_mutex);
    CSurface *pSurface = reinterpret_cast<CSurface *>(pGfxSurface);
    pSurface->SetDesiredResourceState(m_pDevice->m_ResourceStateManager, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ApplyResourceBarriers();
    m_pCommandList->ClearRenderTargetView(CreateRenderTargetView(pSurface, 0, 0, 0), Color, 0, nullptr);
}

//------------------------------------------------------------------------------------------------
D3D12_CPU_DESCRIPTOR_HANDLE CGraphicsContext::CreateRenderTargetView(class CSurface *pSurface, UINT ArraySlice, UINT MipSlice, UINT PlaneSlice)
{
    ID3D12Device *pD3DDevice = m_pDevice->GetD3DDevice();
    UINT incSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT slot = m_NextRTVSlot;
    m_NextRTVSlot = (m_NextRTVSlot) % NumRTVDescriptors;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    cpuHandle.ptr= m_pRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + (incSize * slot);
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtvDesc.Texture2DArray.ArraySize = 1;
    rtvDesc.Texture2DArray.FirstArraySlice = ArraySlice;
    rtvDesc.Texture2DArray.MipSlice = MipSlice;
    rtvDesc.Texture2DArray.PlaneSlice = PlaneSlice;
    ID3D12Resource *pD3DResource = pSurface->GetD3DResource();
    m_pDevice->GetD3DDevice()->CreateRenderTargetView(pD3DResource, &rtvDesc, cpuHandle);
    return cpuHandle;
}

//------------------------------------------------------------------------------------------------
Gem::Result CGraphicsContext::FlushImpl()
{
    try
    {
        ThrowFailedHResult(m_pCommandList->Close());

        ID3D12CommandList *exlist[] =
        {
            m_pCommandList
        };

        m_pCommandQueue->ExecuteCommandLists(1, exlist);

        m_pCommandQueue->Signal(m_pFence, ++m_FenceValue);
        HANDLE hEvent = CreateEvent(nullptr, 0, 0, nullptr);
        m_pFence->SetEventOnCompletion(m_FenceValue, hEvent);
        WaitForSingleObject(hEvent, INFINITE);
        CloseHandle(hEvent);
        m_pCommandAllocator->Reset();

        ThrowFailedHResult(m_pCommandList->Reset(m_pCommandAllocator, nullptr));

        return Result::Success;
    }
    catch (_com_error &e)
    {
        return GemResult(e.Error());
    }
}

//------------------------------------------------------------------------------------------------
Gem::Result CGraphicsContext::Flush()
{
    std::unique_lock<std::mutex> Lock(m_mutex);
    CFunctionSentinel Sentinel(g_Logger, "XCanvasGfxGraphicsContext::Flush", QLog::Category::Debug);
    try
    {
        ThrowGemError(FlushImpl());
    }
    catch (_com_error &e)
    {
        return GemResult(e.Error());
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CGraphicsContext::FlushAndPresent(XCanvasGfxSwapChain *pSwapChain)
{
    std::unique_lock<std::mutex> Lock(m_mutex);
    CFunctionSentinel Sentinel(g_Logger, "XCanvasGfxGraphicsContext::FlushAndPresent", QLog::Category::Debug);

    try
    {
        CSwapChain *pIntSwapChain = reinterpret_cast<CSwapChain *>(pSwapChain);
        pIntSwapChain->m_pSurface->SetDesiredResourceState(m_pDevice->m_ResourceStateManager, D3D12_RESOURCE_STATE_COMMON);
        ApplyResourceBarriers();

        ThrowGemError(FlushImpl());

        ThrowGemError(pIntSwapChain->Present());

        m_pCommandQueue->Signal(m_pFence, ++m_FenceValue);
    }
    catch (GemError &e)
    {
        Sentinel.ReportError(e.Result());
    }

    return Result::Success;
}

GEMMETHODIMP CGraphicsContext::Wait()
{
    std::unique_lock<std::mutex> Lock(m_mutex);
    HANDLE hEvent = CreateEvent(nullptr, 0, 0, nullptr);
    m_pFence->SetEventOnCompletion(m_FenceValue, hEvent);
    WaitForSingleObject(hEvent, INFINITE);
    CloseHandle(hEvent);
    return Result::Success;
}

CGraphicsContext::~CGraphicsContext()
{
    m_pCommandList->Close();

    Wait();
}

//------------------------------------------------------------------------------------------------
void CGraphicsContext::ApplyResourceBarriers()
{
    std::vector<D3D12_RESOURCE_BARRIER> resourceBarriers;
    m_pDevice->m_ResourceStateManager.ResolveResourceBarriers(resourceBarriers);
    m_pCommandList->ResourceBarrier((UINT)resourceBarriers.size(), resourceBarriers.data());
}
