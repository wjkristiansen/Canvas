//================================================================================================
// CanvasGfx12
//================================================================================================

#pragma once

namespace Canvas
{

extern DXGI_FORMAT CanvasFormatToDXGIFormat(Canvas::GfxFormat Fmt);

class CGfxFactory :
	public Gem::TGeneric<XGfxFactory>
{
public:
	CGfxFactory();

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxFactory)
    END_GEM_INTERFACE_MAP()

	GEMMETHOD(CreateDevice)(XGfxDevice **ppDevice);
};

}