//================================================================================================
// Context
//================================================================================================

#pragma once

class CContext :
    public Canvas::XCanvasGfxContext,
    public Gem::CGenericBase
{
    CComPtr<ID3D12CommandQueue> m_pCommandQueue;
    CDevice *m_pDevice = nullptr; // weak pointer

public:
    CContext(CDevice *pDevice, D3D12_COMMAND_LIST_TYPE Type, D3D12_COMMAND_QUEUE_PRIORITY Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    // XCanvasGfxContext methods
    GEMMETHOD_(void, Begin)() final;
    GEMMETHOD_(void, End)() final;
    GEMMETHOD_(void, CopyResource(XCanvasGfxResource *pDest, XCanvasGfxResource *pSource)) final;
};

    