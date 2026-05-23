// Displaced.hlsli - shared types + helpers for the engine's displaced-mesh
// pipeline. Imported by VS/HS/DS/PS so they agree on the per-instance
// constant buffer layout and the control-point / patch I/O structs. Used
// when a material has a GfxDisplacementDesc attached and is paired with
// a procedural patch-grid mesh.

#ifndef CANVAS_DISPLACED_HLSLI
#define CANVAS_DISPLACED_HLSLI

#include "HlslTypes.h"

// Per-frame constants (camera + lights) - same CB the rest of the engine uses.
ConstantBuffer<HlslPerFrameConstants> PerFrame : register(b0);

// Per-instance displaced-mesh constants. One CB per displaced draw.
ConstantBuffer<HlslDisplacedConstants> PerTile : register(b1);

// Heightmap texture (R16_UNORM, with mip chain). The DS samples it to lift
// CP-interpolated XY positions into world Z; the HS samples a coarse mip
// for the curvature term of the LOD computation.
Texture2D<float> Heightmap : register(t0);

// Pre-baked material atlases sampled in the pixel shader by tile UV. All
// three live in the same descriptor table and stay LAYOUT_SHADER_RESOURCE
// after upload (DATA_STATIC SRVs).
Texture2D<float4> AlbedoMap    : register(t1);
Texture2D<float>  AOMap        : register(t2);
Texture2D<float>  RoughnessMap : register(t3);

SamplerState HeightSampler : register(s0);

// Patch control-point payload that flows VS -> HS -> DS.
struct DisplacedControlPoint
{
    float2 TileUV    : TILEUV;     // [0,1]^2 across the tile (used by DS to sample heightmap)
    float3 WorldXY0Z : WORLDXY0Z;  // World-space (x, y, 0) of the CP before height lift
};

// Patch-constant data emitted by the HS once per patch. Holds the tess
// factors the fixed-function tessellator consumes.
struct DisplacedPatchConstants
{
    float EdgeTess[4]   : SV_TessFactor;
    float InsideTess[2] : SV_InsideTessFactor;
};

// Decode a heightmap sample to meters using the material's height scale + bias.
float DecodeHeightMeters(float sample01)
{
    return sample01 * PerTile.HeightScale + PerTile.HeightBias;
}

#endif // CANVAS_DISPLACED_HLSLI