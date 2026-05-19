// PSTerrain.hlsl - terrain pixel stage.
//
// Writes the DS-supplied world position and normal into the deferred
// G-buffer, sampling the pre-baked terrain material atlases (albedo, AO,
// roughness) by TileUV. The slope+altitude blend that produces those
// atlases is resolved on the CPU during BuildTerrainMaterial, so the PS
// is a straight lookup. PSComposite picks the result up via the existing
// G-buffer sampling path.

#include "Terrain.hlsli"

// G-buffer layout matches the existing GBufferOutput in Common.hlsli.
struct GBufferOutput
{
    float4 Normals      : SV_Target0;
    float4 DiffuseColor : SV_Target1;
    float4 WorldPos     : SV_Target2;
    float4 PBR          : SV_Target3;
    float4 Emissive     : SV_Target4;
};

struct PSInput
{
    float2 TexCoord     : TEXCOORD0;
    float3 WorldPos     : WORLDPOS;
    float3 Normal       : NORMAL;
    float4 ClipPosition : SV_Position;
};

GBufferOutput PSTerrain(PSInput input)
{
    GBufferOutput output;

    float3 N = normalize(input.Normal);

    float4 albedo    = AlbedoMap.Sample(HeightSampler, input.TexCoord);
    float  ao        = AOMap.Sample(HeightSampler, input.TexCoord);
    float  roughness = RoughnessMap.Sample(HeightSampler, input.TexCoord);

    output.Normals      = float4(N * 0.5 + 0.5, 1.0);
    output.DiffuseColor = float4(albedo.rgb, 1.0);
    output.WorldPos     = float4(input.WorldPos, 1.0);
    // PBR layout matches the rest of the engine: R=rough, G=metal, B=AO, A=spare.
    // Terrain is fully dielectric so metallic stays 0.
    output.PBR          = float4(roughness, 0.0, ao, 0.0);
    output.Emissive     = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
