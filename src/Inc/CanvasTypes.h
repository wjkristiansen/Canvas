//================================================================================================
// CanvasTypes
//
// Canvas domain vocabulary shared across modules: enums and simple value types with no
// dependencies. Include from any layer without pulling in CanvasCore.h, CanvasGfx.h, or any
// Windows headers.
//================================================================================================
#pragma once

#include <cstdint>

namespace Canvas
{

enum class LightType : uint32_t
{
    Ambient     = 0,
    Point       = 1,
    Directional = 2,
    Spot        = 3,
    Area        = 4,
};

//------------------------------------------------------------------------------------------------
// Pixel/texture format vocabulary shared across Canvas modules.
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
    R11G11B10_Float,
    R8G8B8A8_UNorm,
    R8G8B8A8_UNorm_SRGB,
    R8G8B8A8_UInt,
    R8G8B8A8_Norm,
    R8G8B8A8_Int,
    R8G8B8_UNorm,
    R8G8B8_UInt,
    R8G8B8_Norm,
    R8G8B8_Int,
    R8_UNorm,
    BC1_UNorm,
    BC1_UNorm_SRGB,
    BC2_UNorm,
    BC3_UNorm,
    BC4_UNorm,
    BC4_Norm,
    BC5_UNorm,
    BC5_Norm,
    BC7_UNorm,
    BC7_UNorm_SRGB,
};

//------------------------------------------------------------------------------------------------
// Texture topology shared across Canvas modules. Mirrors the GPU surface model.
enum class GfxSurfaceDimension : uint32_t
{
    Dimension1D   = 0,
    Dimension2D   = 1,
    Dimension3D   = 2,
    DimensionCube = 3,
};

} // namespace Canvas
