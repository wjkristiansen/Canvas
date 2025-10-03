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

#define FOR_EACH_CANVASGFX_INTERFACE(macro, ...) \
    macro(XGfxSurface, __VA_ARGS__) \
    macro(XGfxBuffer, __VA_ARGS__) \
    macro(XGfxSwapChain, __VA_ARGS__) \
    macro(XGfxDevice, __VA_ARGS__) \
    macro(XGfxInstance, __VA_ARGS__) \

    //------------------------------------------------------------------------------------------------
    // Buffer resource
    struct XGfxSurface : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(0x2F05FEAC7133843B);
    };

    //------------------------------------------------------------------------------------------------
    // Buffer resource
    struct XGfxBuffer : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(0xA1DF297C8FA4CF13);
    };

    //------------------------------------------------------------------------------------------------
    struct XGfxSwapChain : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(0x1DEDFC0646129850);

        GEMMETHOD(GetSurface)(XGfxSurface **ppSurface) = 0;
        GEMMETHOD(WaitForLastPresent)() = 0;
    };

    //------------------------------------------------------------------------------------------------
    // Submits command streams to the GPU.
    // Manages synchronization with other command contexts and
    // the CPU.
    // In D3D12, this wraps a command queue and command lists and command allocators.
    // In D3D11, this is wraps an ID3D11DeviceContext
    struct XGfxGraphicsContext : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(0x728AF985153F712D);

        GEMMETHOD(CreateSwapChain)(HWND hWnd, bool Windowed, XGfxSwapChain **ppSwapChain, GfxFormat Format, UINT NumBuffers) = 0;
        GEMMETHOD_(void, CopyBuffer(XGfxBuffer *pDest, XGfxBuffer *pSource)) = 0;
        GEMMETHOD_(void, ClearSurface)(XGfxSurface *pSurface, const float Color[4]) = 0;
        GEMMETHOD(Flush)() = 0;
        GEMMETHOD(FlushAndPresent)(XGfxSwapChain *pSwapChain) = 0;
        GEMMETHOD(Wait)() = 0;
    };

    //------------------------------------------------------------------------------------------------
    // Interface to a graphics device
    struct XGfxDevice : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(0x86D4ABCCCD5FB6EE);

        GEMMETHOD(CreateGraphicsContext)(Canvas::XGfxGraphicsContext **ppGraphicsContext) = 0;
    };

    //------------------------------------------------------------------------------------------------
    struct XGfxInstance : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(0x35CFDC3E089A6F52);
        GEMMETHOD(CreateGfxDevice)(Canvas::XGfxDevice **ppDevice) = 0;
    };
}