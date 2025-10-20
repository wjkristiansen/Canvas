//================================================================================================
// CanvasGfx12
//================================================================================================

#pragma once

extern DXGI_FORMAT CanvasFormatToDXGIFormat(Canvas::GfxFormat Fmt);

class CGfxDeviceFactory :
	public Gem::TGeneric<Canvas::XGfxDeviceFactory>
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxDeviceFactory)
    END_GEM_INTERFACE_MAP()

	CGfxDeviceFactory();
    Gem::Result Initialize() { return Gem::Result::Success; }    
    void Uninitialize() {}

	GEMMETHOD(CreateDevice)(Canvas::XGfxDevice **ppDevice);
};