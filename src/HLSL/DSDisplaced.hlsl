// DSDisplaced.hlsl - domain stage for the engine's displaced-mesh path.
//
// Runs per output vertex produced by the fixed-function tessellator. Given
// barycentric (u, v) in [0, 1]^2 on the quad and the 4 input CPs, computes:
//   - World-space XYZ of the vertex by bilinearly interpolating the CPs'
//     TileUV, sampling the heightmap, and lifting Z.
//   - World-space normal via central differences on the heightmap.
//   - Tile-relative UVs for the PS to sample the material atlases.
//   - Clip-space position for rasterization.

#include "Displaced.hlsli"

struct DSOutput
{
    float2 TexCoord     : TEXCOORD0;     // [0,1]^2 across the tile - PS material UV
    float3 WorldPos     : WORLDPOS;
    float3 Normal       : NORMAL;
    float4 ClipPosition : SV_Position;
};

// Estimate world-space normal at a tile-UV point by central differences on
// the heightmap. The horizontal step is one texel of the chosen mip.
float3 ComputeWorldNormal(float2 tileUv)
{
    uint w, h, nMips;
    Heightmap.GetDimensions(0, w, h, nMips);

    float2 texelUv = float2(1.0 / max(w, 1u), 1.0 / max(h, 1u));
    float hL = DecodeHeightMeters(Heightmap.SampleLevel(HeightSampler, tileUv - float2(texelUv.x, 0), 0));
    float hR = DecodeHeightMeters(Heightmap.SampleLevel(HeightSampler, tileUv + float2(texelUv.x, 0), 0));
    float hD = DecodeHeightMeters(Heightmap.SampleLevel(HeightSampler, tileUv - float2(0, texelUv.y), 0));
    float hU = DecodeHeightMeters(Heightmap.SampleLevel(HeightSampler, tileUv + float2(0, texelUv.y), 0));

    // World step that corresponds to one tile-UV texel.
    float2 worldSize = PerTile.TileOriginAndSize.zw;
    float2 worldPerUvStep = worldSize * texelUv;

    float dhdx = (hR - hL) / (2.0 * worldPerUvStep.x);
    float dhdy = (hU - hD) / (2.0 * worldPerUvStep.y);

    // VSDisplaced flips v -> worldY (see VSDisplaced.hlsl header for the
    // image-vs-bird's-eye handedness rationale).  Chain rule:
    //   dh/d(worldY) = dh/dv * dv/d(worldY)  with  dv/d(worldY) < 0
    // so the geometric normal's y component is -dh/d(worldY) = +dhdy
    // (rather than -dhdy as a positive v -> worldY mapping would give).
    return normalize(float3(-dhdx, +dhdy, 1.0));
}

[domain("quad")]
DSOutput DSDisplaced(
    DisplacedPatchConstants pc,
    float2 uv : SV_DomainLocation,
    OutputPatch<DisplacedControlPoint, 4> patch)
{
    // Bilinear interpolation of patch CPs in tile-UV and world-XY.
    float2 tileUv =
        lerp(lerp(patch[0].TileUV, patch[1].TileUV, uv.x),
             lerp(patch[3].TileUV, patch[2].TileUV, uv.x),
             uv.y);

    float3 worldXY0Z =
        lerp(lerp(patch[0].WorldXY0Z, patch[1].WorldXY0Z, uv.x),
             lerp(patch[3].WorldXY0Z, patch[2].WorldXY0Z, uv.x),
             uv.y);

    // Sample the heightmap (mip 0 for vertex displacement; the curvature
    // term in HS uses a coarser mip).
    float hSample = Heightmap.SampleLevel(HeightSampler, tileUv, 0);
    float worldZ  = DecodeHeightMeters(hSample);

    float4 worldPos4 = float4(worldXY0Z.x, worldXY0Z.y, worldZ, 1.0);
    // Apply per-instance world transform (row-vector convention).
    worldPos4 = mul(worldPos4, PerTile.World);

    DSOutput o;
    o.TexCoord     = tileUv;
    o.WorldPos     = worldPos4.xyz;
    o.Normal       = ComputeWorldNormal(tileUv);
    o.ClipPosition = mul(worldPos4, PerFrame.ViewProj);
    return o;
}