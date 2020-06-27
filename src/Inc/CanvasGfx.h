//================================================================================================
// CanvasGfx
//================================================================================================

#pragma once
#include <QLog.h>

namespace Canvas
{
    enum class GfxFormat : int
    {
        Unknown,
        R32G32B32A32_Float,
        R32G32B32A32_UInt,
        R32G32B32A32_Int,
        R32G32B32_Float,
        R32G32B32_UInt,
        R32G32B32_Int,
        R32G32_Float,
        R32G32_UInt,
        R32G32_Int,
        D32_Float,
        R32_Float,
        R32_UInt,
        R32_Int,
        R16G16B16A16_Float,
        R16G16B16A16_UInt,
        R16G16B16A16_Int,
        R16G16B16A16_UNorm,
        R16G16B16A16_Norm,
        R16G16_Float,
        R16G16_UInt,
        R16G16_Int,
        R16G16_UNorm,
        R16G16_Norm,
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
        R10G10B10A2_UNorm,
        R10G10B10A2_UInt,
        R8G8B8A8_UNorm,
        R8G8B8A8_UInt,
        R8G8B8A8_Norm,
        R8G8B8A8_Int,
        R8G8B8_UNorm,
        R8G8B8_UInt,
        R8G8B8_Norm,
        R8G8B8_Int,
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
    macro(XCanvasGfxDevice, 0x1017) \
    macro(XCanvasGfxContext, 0x1018) \
    macro(XCanvasGfxResource, 0x1019) \
    macro(XCanvasGfxBuffer, 0x1020) \
    macro(XCanvasGfxTexture1D, 0x1021) \
    macro(XCanvasGfxTexture2D, 0x1022) \
    macro(XCanvasGfxTexture3D, 0x1023) \
    macro(XCanvasGfxPipelineState, 0x1024) \
    macro(XCanvasGfxShaderResourceView, 0x1025) \
    macro(XCanvasGfxUnorderedAccessView, 0x1026) \
    macro(XCanvasGfxConstantBufferView, 0x1027) \
    macro(XCanvasGfxDepthStencilView, 0x1028) \
    macro(XCanvasGfxRenderTargetView, 0x1029) \
    macro(XCanvasGfxConstantBuffer, 0x1030) \
    macro(XCanvasGfxUploadBuffer, 0x1031) \
    macro(XCanvasGfxReadbackBuffer, 0x1032) \

    //------------------------------------------------------------------------------------------------
#define ENUM_GS_INTERFACE_ID(iface, value) CanvasGfxIId_##iface=value,
    enum CanvasGfxIId
    {
        FOR_EACH_CANVAS_GS_INTERFACE(ENUM_GS_INTERFACE_ID)
    };

#define CANVAS_GS_INTERFACE_DECLARE(iface) GEM_INTERFACE_DECLARE(CanvasGfxIId_##iface)

    //------------------------------------------------------------------------------------------------
    // Base interface for a CanvasGfx resource.  Inherited by all buffer and texture resource interfaces.
    struct XCanvasGfxResource : public Gem::XGeneric
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGfxResource);
    };

    //------------------------------------------------------------------------------------------------
    // Submits command streams to the GPU.
    // Manages synchronization with other command contexts and
    // the CPU.
    // In D3D12, this wraps a command queue and command lists and command allocators.
    // In D3D11, this is wraps an ID3D11DeviceContext
    struct XCanvasGfxContext : public Gem::XGeneric
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGfxContext);

        // Begins a series of commands
        GEMMETHOD_(void, Begin)() = 0;

        // Ends a series of commands
        GEMMETHOD_(void, End)() = 0;

        // Copies an entire resource
        GEMMETHOD_(void, CopyResource(XCanvasGfxResource *pDest, XCanvasGfxResource *pSource)) = 0;

    };

    //------------------------------------------------------------------------------------------------
    // Buffer resource
    struct XCanvasGfxBuffer : public XCanvasGfxResource
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGfxBuffer);
    };

    //------------------------------------------------------------------------------------------------
    // CPU-Writable resource used for uploading data to GPU memory
    struct XCanvasGfxUploadBuffer : public XCanvasGfxBuffer
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGfxUploadBuffer);
    };

    //------------------------------------------------------------------------------------------------
    // Interface to a GPU device
    struct XCanvasGfxDevice : public Gem::XGeneric
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGfxDevice);

        GEMMETHOD(Present)() = 0;
        GEMMETHOD(Initialize)(HWND hWnd, bool Windowed) = 0;
        GEMMETHOD(AllocateGraphicsContext)(Canvas::XCanvasGfxContext **ppGraphicsContext) = 0;
        //GEMMETHOD(AllocateBuffer)() - 0;
        //GEMMETHOD(AllocateTexture1D) = 0;
        //GEMMETHOD(AllocateTexture1DArray) = 0;
        //GEMMETHOD(AllocateTexture2D) = 0;
        //GEMMETHOD(AllocateTexture2DArray) = 0;
        //GEMMETHOD(AllocateTexture3D) = 0;
        //GEMMETHOD(AllocateTextureCube) = 0;
        // GEMMETHOD(AllocateUploadBuffer)(UINT64 SizeInBytes, XCanvasGfxUploadBuffer **ppUploadBuffer) = 0;
    };

}