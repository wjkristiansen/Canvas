//================================================================================================
// Device12 - Example of external Canvas element registration
//
// This class demonstrates the proper pattern for external plugins:
// 1. Inherit from XGfxDevice (which inherits from XCanvasElement)
// 2. Implement XCanvasElement::Register/Unregister methods
// 3. External callers use device->Register(canvas), NOT canvas->RegisterElement(device)
// 4. Only inside Register/Unregister implementations should canvas->RegisterElement be called
//================================================================================================

#pragma once

#include "D3D12ResourceUtils.h"
#include "GfxElement.h"

//------------------------------------------------------------------------------------------------
class CDevice12 : public Canvas::TGfxElement<Canvas::XGfxDevice>
{
    CComPtr<ID3D12Resource> m_pVertices;
    CComPtr<ID3D12Resource> m_pNormals;
    CComPtr<ID3D12Resource> m_pTextureUVs[4];
    CComPtr<ID3D12Resource> m_pBoneWeights;

public:
    CComPtr<ID3D12Device5> m_pD3DDevice;
    CResourceStateManager m_ResourceStateManager;

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxDevice)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CDevice12();
    ~CDevice12() = default;  // TGfxElement destructor handles Unregister

    Gem::Result Initialize();
    void Uninitialize() {}

    // XGfxDevice methods
    GEMMETHOD(CreateRenderQueue)(Canvas::XGfxRenderQueue **ppRenderQueue) final;
    GEMMETHOD(CreateMaterial)() final;
    GEMMETHOD(CreateSurface)(const Canvas::SurfaceDesc &desc, Canvas::XGfxSurface **ppSurface) final;
    GEMMETHOD(CreateBuffer)(UINT sizeInBytes, Canvas::XGfxBuffer **ppBuffer) final;

    ID3D12Device5 *GetD3DDevice() const { return m_pD3DDevice; }
};
