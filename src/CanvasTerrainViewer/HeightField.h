//================================================================================================
// HeightField
//
// Heightfield asset support for CanvasTerrainViewer.
//
// Concepts:
//   - HeightField: a 2D grid of 16-bit unsigned-normalized height samples plus
//     metadata (world spacing, height scale/bias).
//   - Shared-edge tiling: tiles sized (W, H) cover (W-1, H-1) world cells; the
//     boundary row/column is shared with the neighbor tile so DS sampling on
//     either side of a seam produces identical world positions.
//   - Loaders: WIC on Windows accepts anything WIC decodes (PNG/TIFF/BMP/...),
//     16-bit sources pass through, 8-bit promoted to R16. A minimal PNG/RAW
//     fallback is reserved for future non-Windows builds.
//   - Mesh builders: v1 path produces a CPU XGfxMeshData (regular grid +
//     central-difference normals) for the "validate plumbing" milestone.
//================================================================================================

#pragma once

#include <Gem.hpp>
#include "CanvasCore.h"
#include "CanvasGfx.h"
#include <cstdint>
#include <vector>

namespace Canvas
{
namespace HeightField
{

//------------------------------------------------------------------------------------------------
// Descriptor for a single heightfield tile.
//
// A tile with (Width, Height) texels covers (Width - 1) x (Height - 1) cells
// of world space. The boundary row/column is shared with the neighbor tile:
// same physical world position, same height value. This gives water-tight
// seams between tiles.
//
// Storage is always R16 unsigned-normalized regardless of source format; the
// loader promotes 8-bit sources to R16. If a non-R16 storage path is ever
// needed, add a Canvas::GfxFormat StorageFormat field here.
struct HeightFieldDesc
{
    uint32_t          Width        = 0;     // Texel count along X (covers Width-1 world cells)
    uint32_t          Height       = 0;     // Texel count along Y (covers Height-1 world cells)
    float             DxyMeters    = 1.0f;  // World-space spacing between adjacent texels
    float             HeightScale  = 256.0f; // Meters per (1.0 - 0.0) UNORM range
    float             HeightBias   = 0.0f;  // Meters added after scale
};

//------------------------------------------------------------------------------------------------
// CPU-side heightfield: descriptor + raw R16 samples (always promoted to R16
// regardless of source format, so all consumers see one storage layout).
struct HeightField
{
    HeightFieldDesc       Desc;
    std::vector<uint16_t> Samples; // Length == Desc.Width * Desc.Height, row-major

    // World extent in meters covered by this tile (one less than texel count
    // because the boundary texels are shared with neighbors).
    float WorldWidth()  const { return Desc.Width  > 0 ? (Desc.Width  - 1) * Desc.DxyMeters : 0.0f; }
    float WorldHeight() const { return Desc.Height > 0 ? (Desc.Height - 1) * Desc.DxyMeters : 0.0f; }

    // Sample at integer texel coordinate -> meters.
    float HeightMeters(uint32_t x, uint32_t y) const
    {
        if (x >= Desc.Width || y >= Desc.Height || Samples.empty())
            return Desc.HeightBias;
        const uint16_t s = Samples[static_cast<size_t>(y) * Desc.Width + x];
        return (static_cast<float>(s) / 65535.0f) * Desc.HeightScale + Desc.HeightBias;
    }

    bool IsEmpty() const { return Samples.empty() || Desc.Width == 0 || Desc.Height == 0; }
};

//------------------------------------------------------------------------------------------------
struct LoadOptions
{
    float    DxyMeters   = 1.0f;
    float    HeightScale = 256.0f;
    float    HeightBias  = 0.0f;
};

//------------------------------------------------------------------------------------------------
// Load a heightfield from a file using WIC (Windows). Accepts any format WIC
// can decode; 16-bit grayscale passes through, 8-bit grayscale (or RGB) is
// promoted to R16. Returns true on success and fills outField. On failure,
// outField is left empty and an error is logged via pLogger (if non-null).
//
// The source image dimensions are taken as the tile's full texel grid. Under
// the shared-edge convention a tile sized W x H covers (W-1) x (H-1) world
// cells; neighbors share their boundary row/column verbatim.
bool LoadHeightFieldWIC(
    const wchar_t*     path,
    const LoadOptions& opts,
    HeightField*       outField,
    XLogger*           pLogger = nullptr);

//------------------------------------------------------------------------------------------------
struct TerrainMeshOptions
{
    // World-space origin of texel (0, 0). Tiles laid out on a global grid
    // set OriginX = TileX * (Width-1) * DxyMeters (and similarly for Y).
    float          OriginX  = 0.0f;
    float          OriginY  = 0.0f;
    // Optional material applied to the single mesh group. May be null.
    XGfxMaterial*  pMaterial = nullptr;
    // If > 1, decimate the mesh by this stride in both axes (e.g., Stride=2
    // yields a 1/4-density mesh). Useful for v1 perf testing.
    uint32_t       Stride    = 1;
};

//------------------------------------------------------------------------------------------------
// v1 CPU mesh builder. Produces a single-group XGfxMeshData covering the
// heightfield as a triangulated quad grid. Vertices receive per-vertex
// normals from central-differences against the height samples (clamped at
// boundary texels; in a tiled scene the boundary samples are shared with
// the neighbor, which gives matching normals on both sides).
Gem::Result BuildTerrainMesh(
    XGfxDevice*               pDevice,
    const HeightField&        field,
    const TerrainMeshOptions& opts,
    XGfxMeshData**            ppMesh,
    PCSTR                     name = nullptr);

} // namespace HeightField
} // namespace Canvas
