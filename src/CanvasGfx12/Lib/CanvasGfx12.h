//================================================================================================
// CanvasGfx12
//================================================================================================

#pragma once

extern DXGI_FORMAT CanvasFormatToDXGIFormat(Canvas::GfxFormat Fmt);

class CGfxDeviceFactory :
	public Gem::TGeneric<Canvas::XGfxDeviceFactory>
{
public:
	CGfxDeviceFactory();

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxDeviceFactory)
    END_GEM_INTERFACE_MAP()

	GEMMETHOD(CreateDevice)(Canvas::XGfxDevice **ppDevice);
};