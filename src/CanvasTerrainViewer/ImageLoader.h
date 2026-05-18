//================================================================================================
// ImageLoader
//
// Minimal WIC-backed image loader for 8-bit RGBA images. Used by the terrain
// material atlas path; the heightfield loader has its own WIC code because its
// pixel format and quantization rules are different.
//================================================================================================

#pragma once

#include <Gem.hpp>
#include "CanvasCore.h"
#include <cstdint>
#include <vector>

namespace Canvas
{
namespace TerrainViewer
{

struct ImageRGBA8
{
    uint32_t              Width  = 0;
    uint32_t              Height = 0;
    std::vector<uint8_t>  Pixels;  // Row-major, 4 bytes per pixel (RGBA), length == Width*Height*4

    bool IsEmpty() const { return Pixels.empty() || Width == 0 || Height == 0; }
};

// Load any WIC-decodable image and convert to RGBA8 in CPU memory. Returns
// true on success. On failure, outImage is left empty and an error is logged
// via pLogger (if non-null).
bool LoadImageRGBA8(
    const wchar_t* path,
    ImageRGBA8*    outImage,
    XLogger*       pLogger = nullptr);

} // namespace TerrainViewer
} // namespace Canvas
