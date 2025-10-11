//================================================================================================
// CanvasGfx
//================================================================================================

#pragma once
#include <QLog.h>
#include "Gem.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

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

    //------------------------------------------------------------------------------------------------
    // Buffer resource
    struct XGfxSurface : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(XGfxSurface, 0x2F05FEAC7133843B);
    };

    //------------------------------------------------------------------------------------------------
    enum class MaterialLayerRole
    {
        Albedo,
        Normal,
        Roughness,
        Metallic,
        Emissive,
    };

    //------------------------------------------------------------------------------------------------
    enum MaterialLayerFlags : uint32_t
    {
        None            = 0,
        Decal           = 1 << 0,  // Projected onto surface
        Tiled           = 1 << 1,  // Repeats across UV space
        LODBias         = 1 << 2,  // Applies mip bias
        UVTransform     = 1 << 3,  // Uses custom UV matrix
        Masked          = 1 << 4,  // Uses alpha mask
    };

    //------------------------------------------------------------------------------------------------
    enum class MaterialBlendMode
    {
        Default,
        Additive,
        Multiply,
        AlphaMasked,
        Overlay,
    };

    //------------------------------------------------------------------------------------------------
    struct MaterialLayer
    {
        MaterialLayerRole Role;
        MaterialLayerFlags Flags;
        Math::FloatVector4 BlendFactor;
        Math::FloatVector4 Color;
        XGfxSurface *pSurface;
    };

    //------------------------------------------------------------------------------------------------
    struct
    XGfxMaterial : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(XGfxMaterial, 0xD6E17B2CB8454154);
    };

    //------------------------------------------------------------------------------------------------
    struct
    XGfxMesh : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(XGfxMesh, 0x7EBC2A5A40CC96D3);
    };

    //------------------------------------------------------------------------------------------------
    // Buffer resource
    struct XGfxBuffer : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(XGfxBuffer, 0xA1DF297C8FA4CF13);
    };

    //------------------------------------------------------------------------------------------------
    struct XGfxSwapChain : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(XGfxSwapChain, 0x1DEDFC0646129850);

        GEMMETHOD(GetSurface)(XGfxSurface **ppSurface) = 0;
        GEMMETHOD(WaitForLastPresent)() = 0;
    };

    //------------------------------------------------------------------------------------------------
    // Submits tasks to the graphics subsystem.
    struct XGfxRenderQueue : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(XGfxRenderQueue, 0x728AF985153F712D);

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
        GEM_INTERFACE_DECLARE(XGfxDevice, 0x86D4ABCCCD5FB6EE);

        GEMMETHOD(CreateRenderQueue)(Canvas::XGfxRenderQueue **ppRenderQueue) = 0;
        GEMMETHOD(CreateMaterial)() = 0;
    };

    //------------------------------------------------------------------------------------------------
    struct XGfxDeviceFactory : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(XGfxDeviceFactory, 0x3EE387780593F266);
        GEMMETHOD(CreateDevice)(Canvas::XGfxDevice **ppDevice) = 0;
    };
}

using FnCreateGfxDeviceFactory = Gem::Result (*)(Canvas::XGfxDeviceFactory**);

#ifdef _MSC_VER
#pragma warning(pop)
#endif
