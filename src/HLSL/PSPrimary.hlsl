#include "Common.hlsli"

float4 PSPrimary(PS_INPUT Input) : SV_Target0
{
    float3 N = normalize(Input.Normal);

    // Lambertian diffuse from directional (sun) light
    float NdotL = saturate(dot(N, SunDirection));
    float3 diffuse = SunColor * NdotL;

    // Simple ambient term
    float3 ambient = AmbientLight;

    // Base albedo (white for now)
    float3 albedo = float3(0.8, 0.8, 0.8);

    float3 color = albedo * (ambient + diffuse);

    return float4(color, 1.0);
}