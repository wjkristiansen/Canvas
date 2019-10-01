//================================================================================================
// CanvasGS
//================================================================================================

#pragma once
#include <QLog.h>

namespace Canvas
{
    enum class Format : int
    {
        Unknown,
        RGBA32_Float,
        RGBA32_UInt,
        RGBA32_Int,
        RGB32_Float,
        RGB32_UInt,
        RGB32_Int,
        RG32_Float,
        RG32_UInt,
        RG32_Int,
        D32_Float,
        R32_Float,
        R32_UInt,
        R32_Int,
        RGBA16_Float,
        RGBA16_UInt,
        RGBA16_Int,
        RGBA16_UNorm,
        RGBA16_Norm,
        RG16_Float,
        RG16_UInt,
        RG16_Int,
        RG16_UNorm,
        RG16_Norm,
        R16_Float,
        R16_UInt,
        R16_Int,
        D16_UNorm,
        R16_UNorm,
        R16_Norm,
        D32_Float_S8_UInt_X24,
        R32_Float_X32,
        D24_Unorm_S8_Uint,
        R24_Unorm_X8,
        X24_S8_UInt,
        RGB10A2_UNorm,
        RGB10A2_UInt,
        RGBA8_UNorm,
        RGBA8_UInt,
        RGBA8_Norm,
        RGBA8_Int,
        RG8_UNorm,
        RG8_UInt,
        RG8_Norm,
        RG8_Int,
        BC1_UNorm,
        BC2_UNorm,
        BC3_UNorm,
        BC4_UNorm,
        BC4_Norm,
        BC5_UNorm,
        BC5_Norm,
        BC7_UNorm,
    };

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

    //------------------------------------------------------------------------------------------------
    // Base interface for a CanvasGS resource.  Inherited by all buffer and texture resource interfaces.
    struct XCanvasGSResource : public Gem::XGeneric
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSResource);
    };

    //------------------------------------------------------------------------------------------------
    // Submits command streams to the GPU.
    // Manages synchronization with other command contexts and
    // the CPU.
    // In D3D12, this wraps a command queue and command lists and command allocators.
    // In D3D11, this is wraps an ID3D11DeviceContext
    struct XCanvasGSContext : public Gem::XGeneric
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSContext);

        // Begins a series of commands
        GEMMETHOD_(void, Begin)() = 0;

        // Ends a series of commands
        GEMMETHOD_(void, End)() = 0;

        // Copies an entire resource
        GEMMETHOD_(void, CopyResource(XCanvasGSResource *pDest, XCanvasGSResource *pSource)) = 0;

    };

    //------------------------------------------------------------------------------------------------
    // Buffer resource
    struct XCanvasGSBuffer : public XCanvasGSResource
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSBuffer);
    };

    //------------------------------------------------------------------------------------------------
    // CPU-Writable resource used for uploading data to GPU memory
    struct XCanvasGSUploadBuffer : public XCanvasGSBuffer
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSUploadBuffer);
    };

    //------------------------------------------------------------------------------------------------
    // Interface to a GPU device
    struct XCanvasGSDevice : public Gem::XGeneric
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSDevice);

        GEMMETHOD(Present)() = 0;
        GEMMETHOD(Initialize)(HWND hWnd, bool Windowed) = 0;
        GEMMETHOD(CreateGraphicsContext)(Canvas::XCanvasGSContext **ppGraphicsContext) = 0;
        // GEMMETHOD(AllocateUploadBuffer)(UINT64 SizeInBytes, XCanvasGSUploadBuffer **ppUploadBuffer) = 0;
    };

}