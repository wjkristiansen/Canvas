#include "Common.hlsli"

// Material textures (per-draw descriptor table). SRVs are always bound
// (real or null) so the static sample can run; flag bits gate sample-vs-factor.
Texture2D<float4> AlbedoMap    : register(t4);
Texture2D<float4> NormalMap    : register(t5);
Texture2D<float4> EmissiveMap  : register(t6);
Texture2D<float4> RoughnessMap : register(t7);
Texture2D<float4> MetallicMap  : register(t8);
Texture2D<float4> AOMap        : register(t9);

// Static sampler at s0 (LinearWrap)
SamplerState LinearWrap : register(s0);

GBufferOutput PSPrimary(PS_INPUT Input)
{
    GBufferOutput output;

    float2 uv = Input.TexCoord;
    uint flags = PerObject.MaterialFlags;

    // ----- Albedo
    float4 albedo = PerObject.BaseColorFactor;
    if ((flags & MATERIAL_FLAG_HAS_ALBEDO_TEX) != 0)
        albedo *= AlbedoMap.Sample(LinearWrap, uv);

    // ----- Normal: build TBN if we have a normal map + tangent stream;
    // otherwise emit the interpolated geometric normal.
    float3 N = normalize(Input.Normal);
    if ((flags & MATERIAL_FLAG_HAS_NORMAL_TEX) != 0)
    {
        float3 sampled = NormalMap.Sample(LinearWrap, uv).xyz * 2.0 - 1.0;
        float3 T = normalize(Input.Tangent.xyz);
        float3 B = cross(N, T) * Input.Tangent.w;
        N = normalize(sampled.x * T + sampled.y * B + sampled.z * N);
    }
    output.Normals = float4(N * 0.5 + 0.5, 1.0);

    output.DiffuseColor = float4(albedo.rgb, 1.0);
    output.WorldPos = float4(Input.WorldPosition, 1.0);

    // ----- PBR: Roughness / Metallic / AO.
    float roughness = PerObject.RoughMetalAOFactor.x;
    float metallic  = PerObject.RoughMetalAOFactor.y;
    float ao        = PerObject.RoughMetalAOFactor.z;
    if ((flags & MATERIAL_FLAG_HAS_ROUGH_TEX) != 0)
        roughness *= RoughnessMap.Sample(LinearWrap, uv).r;
    if ((flags & MATERIAL_FLAG_HAS_METAL_TEX) != 0)
        metallic  *= MetallicMap.Sample(LinearWrap, uv).r;
    if ((flags & MATERIAL_FLAG_HAS_AO_TEX) != 0)
        ao        *= AOMap.Sample(LinearWrap, uv).r;
    output.PBR = float4(saturate(roughness), saturate(metallic), saturate(ao), 0.0);

    // ----- Emissive.
    float3 emissive = PerObject.EmissiveFactor.rgb;
    if ((flags & MATERIAL_FLAG_HAS_EMISSIVE_TEX) != 0)
        emissive *= EmissiveMap.Sample(LinearWrap, uv).rgb;
    output.Emissive = float4(emissive, 1.0);

    return output;
}