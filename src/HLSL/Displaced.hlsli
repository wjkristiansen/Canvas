// Displaced.hlsli - shared types + helpers for the engine's displaced-mesh
// pipeline. Imported by VS/HS/DS/PS so they agree on the per-instance
// constant buffer layout and the control-point / patch I/O structs. Used
// when a material has a GfxDisplacementDesc attached and is paired with
// a patch-grid mesh.

#ifndef CANVAS_DISPLACED_HLSLI
#define CANVAS_DISPLACED_HLSLI

#include "HlslTypes.h"

// Per-frame constants (camera + lights) - same CB the rest of the engine uses.
ConstantBuffer<HlslPerFrameConstants> PerFrame : register(b0);

// Per-instance displaced-mesh constants. One CB per displaced draw.
ConstantBuffer<HlslDisplacedConstants> PerTile : register(b1);

// Displacement map (single-channel UNORM, with mip chain).  The DS samples
// it to offset each CP-bilerped base position along its bilerped normal by
// the decoded sample; the HS samples a coarse mip for the curvature term
// of the LOD computation.
Texture2D<float> DisplacementMap : register(t0);

// Pre-baked material atlases sampled in the pixel shader by tile UV. All
// three live in the same descriptor table and stay LAYOUT_SHADER_RESOURCE
// after upload (DATA_STATIC SRVs).
Texture2D<float4> AlbedoMap    : register(t1);
Texture2D<float>  AOMap        : register(t2);
Texture2D<float>  RoughnessMap : register(t3);

// Per-CP vertex streams.  The displaced path runs without IA bindings; the
// VS reads CP attributes from these SRVs indexed by SV_VertexID.
//
//   Positions  - mesh-local-space CP positions (W=1).
//   UV0s       - per-CP UVs into the bound displacement map.
//   Normals    - per-CP base-surface normals along which each CP is
//                displaced.  For a flat XY-plane base supply (0, 0, 1, 0).
StructuredBuffer<float4> Positions : register(t4);
StructuredBuffer<float2> UV0s      : register(t5);
StructuredBuffer<float4> Normals   : register(t6);

SamplerState MapSampler : register(s0);

// Patch control-point payload that flows VS -> HS -> DS.
struct DisplacedControlPoint
{
    float2 TileUV   : TILEUV;    // [0,1]^2 across the tile (used by DS to sample displacement map)
    float3 WorldPos : WORLDPOS;  // World-space base position of the CP (pre-displacement)
    float3 Normal   : NORMAL;    // World-space direction along which the displacement extends the CP
};

// Patch-constant data emitted by the HS once per patch. Holds the tess
// factors the fixed-function tessellator consumes.
struct DisplacedPatchConstants
{
    float EdgeTess[4]   : SV_TessFactor;
    float InsideTess[2] : SV_InsideTessFactor;
};

// Decode a displacement-map sample to world units using the material's
// scale + bias.  The decoded value is the signed offset along the CP's
// base normal direction (Displaced.hlsli::DisplacedControlPoint::Normal).
float DecodeDisplacement(float sample01)
{
    return sample01 * PerTile.MapScale + PerTile.MapBias;
}

#endif // CANVAS_DISPLACED_HLSLI