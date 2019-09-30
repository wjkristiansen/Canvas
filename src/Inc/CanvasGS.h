//================================================================================================
// CanvasGS
//================================================================================================

#pragma once
#include <QLog.h>

namespace Canvas
{
#define FOR_EACH_CANVAS_GS_FORMAT(macro) \
    macro(UNKNOWN) \
    macro(R32G32B32A32_TYPELESS) \
    macro(R32G32B32A32_FLOAT) \
    macro(R32G32B32A32_UINT) \
    macro(R32G32B32A32_SINT) \
    macro(R32G32B32_TYPELESS) \
    macro(R32G32B32_FLOAT) \
    macro(R32G32B32_UINT) \
    macro(R32G32B32_SINT) \
    macro(R16G16B16A16_TYPELESS) \
    macro(R16G16B16A16_FLOAT) \
    macro(R16G16B16A16_UNORM) \
    macro(R16G16B16A16_UINT) \
    macro(R16G16B16A16_SNORM) \
    macro(R16G16B16A16_SINT) \
    macro(R32G32_TYPELESS) \
    macro(R32G32_FLOAT) \
    macro(R32G32_UINT) \
    macro(R32G32_SINT) \
    macro(R32G8X24_TYPELESS) \
    macro(D32_FLOAT_S8X24_UINT) \
    macro(R32_FLOAT_X8X24_TYPELESS) \
    macro(X32_TYPELESS_G8X24_UINT) \
    macro(R10G10B10A2_TYPELESS) \
    macro(R10G10B10A2_UNORM) \
    macro(R10G10B10A2_UINT) \
    macro(R11G11B10_FLOAT) \
    macro(R8G8B8A8_TYPELESS) \
    macro(R8G8B8A8_UNORM) \
    macro(R8G8B8A8_UNORM_SRGB) \
    macro(R8G8B8A8_UINT) \
    macro(R8G8B8A8_SNORM) \
    macro(R8G8B8A8_SINT) \
    macro(R16G16_TYPELESS) \
    macro(R16G16_FLOAT) \
    macro(R16G16_UNORM) \
    macro(R16G16_UINT) \
    macro(R16G16_SNORM) \
    macro(R16G16_SINT) \
    macro(R32_TYPELESS) \
    macro(D32_FLOAT) \
    macro(R32_FLOAT) \
    macro(R32_UINT) \
    macro(R32_SINT) \
    macro(R24G8_TYPELESS) \
    macro(D24_UNORM_S8_UINT) \
    macro(R24_UNORM_X8_TYPELESS) \
    macro(X24_TYPELESS_G8_UINT) \
    macro(R8G8_TYPELESS) \
    macro(R8G8_UNORM) \
    macro(R8G8_UINT) \
    macro(R8G8_SNORM) \
    macro(R8G8_SINT) \
    macro(R16_TYPELESS) \
    macro(R16_FLOAT) \
    macro(D16_UNORM) \
    macro(R16_UNORM) \
    macro(R16_UINT) \
    macro(R16_SNORM) \
    macro(R16_SINT) \
    macro(R8_TYPELESS) \
    macro(R8_UNORM) \
    macro(R8_UINT) \
    macro(R8_SNORM) \
    macro(R8_SINT) \
    macro(A8_UNORM) \
    macro(R1_UNORM) \
    macro(R9G9B9E5_SHAREDEXP) \
    macro(R8G8_B8G8_UNORM) \
    macro(G8R8_G8B8_UNORM) \
    macro(BC1_TYPELESS) \
    macro(BC1_UNORM) \
    macro(BC1_UNORM_SRGB) \
    macro(BC2_TYPELESS) \
    macro(BC2_UNORM) \
    macro(BC2_UNORM_SRGB) \
    macro(BC3_TYPELESS) \
    macro(BC3_UNORM) \
    macro(BC3_UNORM_SRGB) \
    macro(BC4_TYPELESS) \
    macro(BC4_UNORM) \
    macro(BC4_SNORM) \
    macro(BC5_TYPELESS) \
    macro(BC5_UNORM) \
    macro(BC5_SNORM) \
    macro(B5G6R5_UNORM) \
    macro(B5G5R5A1_UNORM) \
    macro(B8G8R8A8_UNORM) \
    macro(B8G8R8X8_UNORM) \
    macro(R10G10B10_XR_BIAS_A2_UNORM) \
    macro(B8G8R8A8_TYPELESS) \
    macro(B8G8R8A8_UNORM_SRGB) \
    macro(B8G8R8X8_TYPELESS) \
    macro(B8G8R8X8_UNORM_SRGB) \
    macro(BC6H_TYPELESS) \
    macro(BC6H_UF16) \
    macro(BC6H_SF16) \
    macro(BC7_TYPELESS) \
    macro(BC7_UNORM) \
    macro(BC7_UNORM_SRGB) \
    macro(AYUV) \
    macro(Y410) \
    macro(Y416) \
    macro(NV12) \
    macro(P010) \
    macro(P016) \
    macro(420_OPAQUE) \
    macro(YUY2) \
    macro(Y210) \
    macro(Y216) \
    macro(NV11) \
    macro(AI44) \
    macro(IA44) \
    macro(P8) \
    macro(A8P8) \
    macro(B4G4R4A4_UNORM) \
    macro(P208) \
    macro(V208) \
    macro(V408) \

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
    // Submits command streams to the GPU.
    // Manages synchronization with other command contexts and
    // the CPU.
    // In D3D12, this wraps a command queue and command lists and command allocators.
    // In D3D11, this is wraps an ID3D11DeviceContext
    struct XCanvasGSContext : public Gem::XGeneric
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSContext);

        // Begins a series of commands
        GEMMETHOD(Begin)() = 0;

        // Ends a series of commands
        GEMMETHOD(End)() = 0;

        // Copies an entire resource
        GEMMETHOD(CopyResource(XCanvasGSResource *pDest, XCanvasGSResource *pSource)) = 0;

    };

    //------------------------------------------------------------------------------------------------
    // Base interface for a CanvasGS resource.  Inherited by all buffer and texture resource interfaces.
    struct XCanvasGSResource : public Gem::XGeneric
    {
        CANVAS_GS_INTERFACE_DECLARE(XCanvasGSResource);
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