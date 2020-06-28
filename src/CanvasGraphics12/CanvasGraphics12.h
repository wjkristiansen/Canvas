//================================================================================================
// CanvasGraphics12
//================================================================================================

#pragma once

extern DXGI_FORMAT CanvasFormatToDXGIFormat(Canvas::GfxFormat Fmt);
extern Result GEMAPI CreateCanvasGraphicsDevice(_In_ class CCanvas *pCanvas, _Outptr_result_nullonfailure_ XCanvasGfxDevice **pGraphicsDevice);

extern QLog::CBasicLogger g_Logger;

