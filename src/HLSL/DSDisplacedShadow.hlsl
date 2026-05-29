// DSDisplacedShadow.hlsl - depth-only domain stage for the engine's
// displaced-mesh shadow pass.
//
// Mirrors DSDisplaced.hlsl for the world-space position computation
// (bilerp CP base + sample displacement map + offset along bilerped
// normal) but projects with the light's view+proj instead of the
// camera's, and emits only SV_Position so the PSO can omit the PS
// entirely and write only depth into the shadow atlas tile.
//
// Reuses Displaced.hlsli's PerFrame (b0) + PerTile (b1) + DisplacementMap (t0)
// + MapSampler (s0) bindings unchanged; adds a tiny b2 carrying just
// the light's world->shadow-clip matrix.  VSDisplaced and HSDisplaced
// are reused unchanged (their LOD logic keys off the camera position,
// which is what we want for shadow tessellation to match receiver
// tessellation and avoid Peter Panning).

#include "Displaced.hlsli"

// Per-shadow-draw constants.  One CB per (light x tile) shadow draw.
// The matrix is reverse-Z (matches PerspectiveReverseZ): receivers
// nearest the light end at z=1, receivers at the far plane at z=0.
struct HlslShadowConstants
{
    ROW_MAJOR float4x4 ShadowViewProj;
};

ConstantBuffer<HlslShadowConstants> Shadow : register(b2);

struct DSShadowOutput
{
    float4 ClipPosition : SV_Position;
};

[domain("quad")]
DSShadowOutput DSDisplacedShadow(
    DisplacedPatchConstants pc,
    float2 uv : SV_DomainLocation,
    OutputPatch<DisplacedControlPoint, 4> patch)
{
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

    float hSample = DisplacementMap.SampleLevel(MapSampler, tileUv, 0);
    float disp    = DecodeDisplacement(hSample);
    float3 worldPos = basePos + disp * baseN;

    DSShadowOutput o;
    o.ClipPosition = mul(float4(worldPos, 1.0), Shadow.ShadowViewProj);
    return o;
}
