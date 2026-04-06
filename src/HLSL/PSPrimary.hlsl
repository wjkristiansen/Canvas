#include "Common.hlsli"

GBufferOutput PSPrimary(PS_INPUT Input)
{
    GBufferOutput output;

    // Store world-space normal (normalized, mapped to [0,1] for unorm storage)
    float3 N = normalize(Input.Normal);
    output.Normals = float4(N * 0.5 + 0.5, 1.0);

    // Store diffuse albedo
    float3 albedo = float3(0.8, 0.8, 0.8);
    output.DiffuseColor = float4(albedo, 1.0);

    // Store world position for accurate point/spot lighting in composite pass.
    output.WorldPos = float4(Input.WorldPosition, 1.0);

    return output;
}