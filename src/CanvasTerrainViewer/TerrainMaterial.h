//================================================================================================
// TerrainMaterial
//
// CPU-side builder that bakes a heightfield + a 2x2 material atlas into three
// per-tile composite GPU textures (albedo, AO, roughness) for the v1 terrain
// material. The blend between atlas slots is driven by per-texel altitude and
// slope derived from the heightfield.
//
// Atlas layout (2x2):
//
//     +-------+-------+
//     | grass | rock  |   <- top-left, top-right
//     +-------+-------+
//     | sand  | snow  |   <- bottom-left, bottom-right
//     +-------+-------+
//
// Blend weights:
//   sand   = falls off with altitude (sea level only)
//   grass  = low-slope, mid-altitude
//   rock   = high-slope, any altitude (dominant on steep terrain)
//   snow   = high-altitude, low-slope
//
// World UVs into each atlas slot wrap at AtlasRepeatMeters so the per-material
// detail tiles across the terrain rather than stretching to fill the tile.
//================================================================================================

#pragma once

#include <Gem.hpp>
#include "CanvasCore.h"
#include "CanvasGfx.h"
#include "HeightField.h"
#include "ImageLoader.h"

namespace Canvas
{
namespace TerrainViewer
{

struct TerrainMaterialOptions
{
    // World-space origin of texel (0, 0) in the heightfield. Used to compute
    // world XY at each terrain texel so the atlas tiles wrap consistently
    // across multiple terrain tiles.
    float    OriginX           = 0.0f;
    float    OriginY           = 0.0f;
    // World-space period (meters) over which one atlas slot tile repeats.
    float    AtlasRepeatMeters = 8.0f;
    // Absolute-altitude thresholds in meters. The bands defined by these
    // anchor sand and snow to physical altitude so multi-tile scenes have
    // consistent biome boundaries: a plain next to a mountain stays a plain.
    //   sand     = 1 above SandMaxMeters (smoothstep to SandFadeMeters)
    //   snow     = 1 above SnowFadeMeters (smoothstep from SnowMinMeters)
    // Defaults are tuned for the default sample heightmap (heightscale 64m).
    float    SandMaxMeters     = 4.0f;
    float    SandFadeMeters    = 8.0f;
    float    SnowMinMeters     = 42.0f;
    float    SnowFadeMeters    = 52.0f;
    // Slope threshold band: rock weight ramps from 0 at SlopeRockMin to 1 at
    // SlopeRockMax. Slope is computed as 1 - normal.z (0 = flat, 1 = vertical).
    float    SlopeRockMin      = 0.10f;
    float    SlopeRockMax      = 0.30f;
};

struct TerrainMaterialOutputs
{
    Gem::TGemPtr<XGfxSurface> pAlbedo;     // R8G8B8A8_UNorm, terrain-tile-sized
    Gem::TGemPtr<XGfxSurface> pAO;         // R8_UNorm, terrain-tile-sized
    Gem::TGemPtr<XGfxSurface> pRoughness;  // R8_UNorm, terrain-tile-sized
};

// Bake composite textures and upload them to the device. albedoAtlas / ormAtlas
// must be 2x2 packed atlases produced by gen_material_atlas.py (RGBA8;
// ormAtlas's RGB = AO, Roughness, Metallic; alpha ignored).
//
// On success, outputs.pAlbedo / pAO / pRoughness are populated. Metallic is
// always 0 for our four materials so no metallic surface is produced - the
// caller binds nothing and lets the material factor be 0.
Gem::Result BuildTerrainMaterial(
    XGfxDevice*                     pDevice,
    const HeightField::HeightField& field,
    const ImageRGBA8&               albedoAtlas,
    const ImageRGBA8&               ormAtlas,
    const TerrainMaterialOptions&   opts,
    TerrainMaterialOutputs*         outputs,
    XLogger*                        pLogger = nullptr);

} // namespace TerrainViewer
} // namespace Canvas
