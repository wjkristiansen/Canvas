// DSDisplaced.hlsl - domain stage for the engine's displaced-mesh path.
//
// Runs per output vertex produced by the fixed-function tessellator.
// Given barycentric (u, v) in [0, 1]^2 on the quad and the 4 input CPs:
//   1. Bilerps CP base position / UV / normal at the output vertex.
//   2. Samples the displacement map at the bilerped UV and decodes to a
//      world-unit scalar.
//   3. Displaces the base position along the bilerped normal by that
//      scalar.
//   4. Computes the displaced surface normal from the patch's tangent
//      basis and the displacement-map gradient (general formulation;
//      reduces to the flat-XY heightfield case when all CP normals
//      point +Z and the patch sits in the XY plane).
//   5. Projects to clip space.

#include "Displaced.hlsli"

struct DSOutput
{
    float2 TexCoord     : TEXCOORD0;     // [0,1]^2 across the tile - PS material UV
    float3 WorldPos     : WORLDPOS;
    float3 Normal       : NORMAL;
    float4 ClipPosition : SV_Position;
};

// Displaced surface normal at (tileUv) using:
//   - The patch's base tangent basis (CP0->CP1 spans U, CP0->CP3 spans V)
//   - The bilerped base normal at the sample point (baseN)
//   - The displacement-map gradient (dh/du, dh/dv)
// Surface tangents:
//   dD/du = dP/du + (dh/du) * baseN
//   dD/dv = dP/dv + (dh/dv) * baseN
// Normal: normalize(cross(dD/du, dD/dv)), flipped to agree with baseN's
// orientation so the caller's choice of (u, v) handedness doesn't matter.
float3 ComputeDisplacedNormal(float2 tileUv,
                              float3 baseN,
                              OutputPatch<DisplacedControlPoint, 4> patch)
{
    uint w, h, nMips;
    DisplacementMap.GetDimensions(0, w, h, nMips);
    float2 texelUv = float2(1.0 / max(w, 1u), 1.0 / max(h, 1u));

    float hL = DecodeDisplacement(DisplacementMap.SampleLevel(MapSampler, tileUv - float2(texelUv.x, 0), 0));
    float hR = DecodeDisplacement(DisplacementMap.SampleLevel(MapSampler, tileUv + float2(texelUv.x, 0), 0));
    float hD = DecodeDisplacement(DisplacementMap.SampleLevel(MapSampler, tileUv - float2(0, texelUv.y), 0));
    float hU = DecodeDisplacement(DisplacementMap.SampleLevel(MapSampler, tileUv + float2(0, texelUv.y), 0));
    float dHdu_perUV = (hR - hL) / (2.0 * texelUv.x);
    float dHdv_perUV = (hU - hD) / (2.0 * texelUv.y);

    // Base tangents in world units per UV unit, derived from the patch CPs.
    // Guard against degenerate (zero-area) patches.
    float duPatch = patch[1].TileUV.x - patch[0].TileUV.x;
    float dvPatch = patch[3].TileUV.y - patch[0].TileUV.y;
    duPatch = (abs(duPatch) > 1e-8) ? duPatch : ((duPatch >= 0) ? 1e-8 : -1e-8);
    dvPatch = (abs(dvPatch) > 1e-8) ? dvPatch : ((dvPatch >= 0) ? 1e-8 : -1e-8);
    float3 dPdu = (patch[1].WorldPos - patch[0].WorldPos) / duPatch;
    float3 dPdv = (patch[3].WorldPos - patch[0].WorldPos) / dvPatch;

    float3 dDdu = dPdu + dHdu_perUV * baseN;
    float3 dDdv = dPdv + dHdv_perUV * baseN;
    float3 n    = cross(dDdu, dDdv);
    // Flip if the (u, v) parameterization is negatively oriented w.r.t.
    // the supplied baseN, so the result is always on baseN's side.
    if (dot(n, baseN) < 0.0)
        n = -n;
    return normalize(n);
}

[domain("quad")]
DSOutput DSDisplaced(
    DisplacedPatchConstants pc,
    float2 uv : SV_DomainLocation,
    OutputPatch<DisplacedControlPoint, 4> patch)
{
    // Bilinear interpolation of patch CPs.  Position and normal are
    // already in world space (VS applied PerTile.World).
    float2 tileUv =
        lerp(lerp(patch[0].TileUV, patch[1].TileUV, uv.x),
             lerp(patch[3].TileUV, patch[2].TileUV, uv.x),
             uv.y);
    float3 basePos =
        lerp(lerp(patch[0].WorldPos, patch[1].WorldPos, uv.x),
             lerp(patch[3].WorldPos, patch[2].WorldPos, uv.x),
             uv.y);
    float3 baseN = normalize(
        lerp(lerp(patch[0].Normal, patch[1].Normal, uv.x),
             lerp(patch[3].Normal, patch[2].Normal, uv.x),
             uv.y));

    // Displace along the base normal by the decoded sample (world units).
    float hSample = DisplacementMap.SampleLevel(MapSampler, tileUv, 0);
    float disp    = DecodeDisplacement(hSample);
    float3 worldPos = basePos + disp * baseN;

    DSOutput o;
    o.TexCoord     = tileUv;
    o.WorldPos     = worldPos;
    o.Normal       = ComputeDisplacedNormal(tileUv, baseN, patch);
    o.ClipPosition = mul(float4(worldPos, 1.0), PerFrame.ViewProj);
    return o;
}