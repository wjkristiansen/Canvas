//================================================================================================
// Context
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

CContext::CContext(CDevice *pDevice, D3D12_COMMAND_LIST_TYPE Type, D3D12_COMMAND_QUEUE_PRIORITY Priority)
{
    ID3D12Device5 *pD3DDevice = pDevice->GetD3DDevice();
    CComPtr<ID3D12CommandQueue> pCQ;
    D3D12_COMMAND_QUEUE_DESC CQDesc;
    CQDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    CQDesc.NodeMask = 1;
    CQDesc.Priority = Priority;
    CQDesc.Type = Type;
    ThrowGemError(HResultToResult(pD3DDevice->CreateCommandQueue(&CQDesc, IID_PPV_ARGS(&pCQ))));
    m_pCommandQueue.Attach(pCQ.Detach());
}

GEMMETHODIMP_(void) CContext::Begin()
{
}

GEMMETHODIMP_(void) CContext::End()
{
}

GEMMETHODIMP_(void) CContext::CopyResource(XCanvasGSResource *pDest, XCanvasGSResource *pSource)
{
}
