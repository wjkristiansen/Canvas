// Terrain.hlsli - shared types + helpers for the terrain pipeline (v2 GPU
// tessellation). Imported by VS/HS/DS/PS so they agree on the per-tile
// constant buffer layout and the control-point / patch I/O structs.

#ifndef CANVAS_TERRAIN_HLSLI
#define CANVAS_TERRAIN_HLSLI

#include "HlslTypes.h"

// Per-frame constants (camera + lights) - same CB the rest of the engine uses.
ConstantBuffer<HlslPerFrameConstants> PerFrame : register(b0);

// Per-tile terrain constants. One CB per terrain tile draw.
ConstantBuffer<HlslTerrainConstants>  PerTile  : register(b1);

// Heightmap texture (R16_UNORM, with mip chain). The DS samples it to lift
// CP-interpolated XY positions into world Z. v1 LOD scaffold uses a single
// mip; v2 LOD picks the mip whose texel size matches the patch's screen
// extent for stable curvature reads.
Texture2D<float> Heightmap : register(t0);

SamplerState HeightSampler : register(s0);

// Patch control-point payload that flows VS -> HS -> DS.
struct TerrainControlPoint
{
    float2 TileUV    : TILEUV;     // [0,1]^2 across the tile (used by DS to sample heightmap)
    float3 WorldXY0Z : WORLDXY0Z;  // World-space (x, y, 0) of the CP before height lift
};

// Patch-constant data emitted by the HS once per patch. Holds the tess
// factors the fixed-function tessellator consumes.
struct TerrainPatchConstants
{
    float EdgeTess[4]   : SV_TessFactor;
    float InsideTess[2] : SV_InsideTessFactor;
};

// Decode a R16_UNorm sample to meters using the tile's height scale + bias.
float DecodeHeightMeters(float sample01)
{
    return sample01 * PerTile.HeightScale + PerTile.HeightBias;
}

#endif // CANVAS_TERRAIN_HLSLI
