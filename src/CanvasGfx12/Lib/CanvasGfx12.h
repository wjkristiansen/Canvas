//================================================================================================
// CanvasGfx12
//================================================================================================

#pragma once

namespace Canvas
{

extern DXGI_FORMAT CanvasFormatToDXGIFormat(GfxFormat Fmt);

class CGfxDeviceFactory :
	public Gem::TGeneric<XGfxDeviceFactory>
{
public:
	CGfxDeviceFactory();

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxDeviceFactory)
    END_GEM_INTERFACE_MAP()

	GEMMETHOD(CreateDevice)(XGfxDevice **ppDevice);
};

}