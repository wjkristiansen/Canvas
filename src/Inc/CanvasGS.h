//================================================================================================
// CanvasGS
//================================================================================================

#pragma once
#include <QLog.h>

namespace Canvas
{
#define FOR_EACH_CANVAS_GS_INTERFACE(macro) \
    macro(XCanvasGSDevice, 0x1017) \
    macro(XCanvasGSContext, 0x1018) \
    macro(XCanvasGSResource, 0x1019) \
    macro(XCanvasGSBuffer, 0x1020) \
    macro(XCanvasGSTexture1D, 0x1021) \
    macro(XCanvasGSTexture2D, 0x1022) \
    macro(XCanvasGSTexture3D, 0x1023) \
    macro(XCanvasGSPipelineState, 0x1024) \
    macro(XCanvasGSShaderResourceView, 0x1025) \
    macro(XCanvasGSUnorderedAccessView, 0x1026) \
    macro(XCanvasGSConstantBufferView, 0x1027) \
    macro(XCanvasGSDepthStencilView, 0x1028) \
    macro(XCanvasGSRenderTargetView, 0x1029) \
    macro(XCanvasGSConstantBuffer, 0x1030) \
    macro(XCanvasGSUploadBuffer, 0x1031) \
    macro(XCanvasGSReadbackBuffer, 0x1032) \

    //------------------------------------------------------------------------------------------------
#define ENUM_GS_INTERFACE_ID(iface, value) CanvasGSIId_##iface=value,
    enum CanvasGSIId
    {
        FOR_EACH_CANVAS_GS_INTERFACE(ENUM_GS_INTERFACE_ID)
    };

#define CANVAS_GS_INTERFACE_DECLARE(iface) GEM_INTERFACE_DECLARE(CanvasGSIId_##iface)

    struct XCanvasGSContext : public Gem::XGeneric
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSContext);
    };

    struct XCanvasGSResource : public Gem::XGeneric
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSResource);
    };

    struct XCanvasGSBuffer : public XCanvasGSResource
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSBuffer);
    };

    struct XCanvasGSUploadBuffer : public XCanvasGSBuffer
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSUploadBuffer);
    };

    //------------------------------------------------------------------------------------------------
    struct XCanvasGSDevice : public Gem::XGeneric
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSDevice);

        GEMMETHOD(Present)() = 0;
        GEMMETHOD(Initialize)(HWND hWnd, bool Windowed) = 0;
        // GEMMETHOD(AllocateUploadBuffer)(UINT64 SizeInBytes, XCanvasGSUploadBuffer **ppUploadBuffer) = 0;
    };

}