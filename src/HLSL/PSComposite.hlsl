// Composition pixel shader — reads G-buffers and performs deferred lighting.
// Outputs final lit color to the back buffer (SV_Target0).

// G-buffer textures bound via descriptor table
Texture2D GBufferNormals      : register(t0);
Texture2D GBufferDiffuseColor : register(t1);

// Point sampler for exact texel fetch (G-buffer is 1:1 with screen)
SamplerState PointSampler : register(s0);

#include "HlslTypes.h"

ConstantBuffer<HlslPerFrameConstants> PerFrame : register(b0);

struct FSInput
{
    float2 TexCoord : TEXCOORD0;
    float4 Position : SV_Position;
};

float4 PSComposite(FSInput input) : SV_Target0
{
    // Sample G-buffers
    float4 normalSample  = GBufferNormals.Sample(PointSampler, input.TexCoord);
    float4 diffuseSample = GBufferDiffuseColor.Sample(PointSampler, input.TexCoord);

    // Skip pixels with no geometry (alpha == 0 means nothing was written)
    if (normalSample.a == 0.0)
        discard;

    // Decode world-space normal from [0,1] → [-1,1]
    float3 N = normalize(normalSample.rgb * 2.0 - 1.0);
    float3 albedo = diffuseSample.rgb;

    // Accumulate lighting from all active lights
    float3 totalLight = float3(0.0, 0.0, 0.0);

    for (uint i = 0; i < PerFrame.LightCount && i < MAX_LIGHTS_PER_REGION; ++i)
    {
        HlslLight light = PerFrame.Lights[i];

        if (light.Type == LIGHT_DIRECTIONAL)
        {
            float3 L = normalize(light.DirectionOrPosition.xyz);
            float NdotL = saturate(dot(N, L));
            totalLight += light.Color.rgb * NdotL;
        }
        else if (light.Type == LIGHT_AMBIENT)
        {
            totalLight += light.Color.rgb;
        }
    }

    float3 color = albedo * totalLight;
    return float4(color, 1.0);
}
