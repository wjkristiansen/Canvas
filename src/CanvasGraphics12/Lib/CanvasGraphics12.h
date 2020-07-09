//================================================================================================
// CanvasGraphics12
//================================================================================================

#pragma once

extern DXGI_FORMAT CanvasFormatToDXGIFormat(Canvas::GfxFormat Fmt);

class CCanvasGfx :
	public XCanvasGfx,
	public Gem::CGenericBase
{
	QLog::CBasicLogger m_Logger;

	static CCanvasGfx *m_pThis;

public:
	CCanvasGfx(QLog::CLogClient *pLogClient);

	QLog::CBasicLogger &Logger() { return m_Logger; }

    BEGIN_GEM_INTERFACE_MAP(Gem::CGenericBase)
        GEM_INTERFACE_ENTRY(XCanvasGfx)
    END_GEM_INTERFACE_MAP()

    static CCanvasGfx *GetSingleton() { return m_pThis; }
	GEMMETHOD(CreateCanvasGfxDevice)(XCanvasGfxDevice **ppDevice);
};