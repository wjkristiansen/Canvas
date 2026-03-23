// Composition pixel shader — reads G-buffers and performs deferred lighting.
// Outputs final lit color to the back buffer (SV_Target0).

// G-buffer textures bound via descriptor table
Texture2D GBufferNormals      : register(t0);
Texture2D GBufferDiffuseColor : register(t1);

// Point sampler for exact texel fetch (G-buffer is 1:1 with screen)
SamplerState PointSampler : register(s0);

// Lighting constants (same layout as PerFrameConstants in Common.hlsli)
cbuffer CompositeConstants : register(b0)
{
    row_major float4x4 ViewProj;
    float3 CameraWorldPos;
    float _pad0;
    float3 AmbientLight;
    float _pad1;
    float3 SunDirection;
    float _pad2;
    float3 SunColor;
    float _pad3;
};

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

    // Decode world-space normal from [0,1] → [-1,1]
    float3 N = normalize(normalSample.rgb * 2.0 - 1.0);

    float3 albedo = diffuseSample.rgb;

    // Skip lighting for pixels with no geometry (alpha == 0 means nothing was written)
    if (normalSample.a == 0.0)
        discard;

    // Lambertian diffuse from directional (sun) light
    float NdotL = saturate(dot(N, SunDirection));
    float3 diffuse = SunColor * NdotL;

    float3 color = albedo * (AmbientLight + diffuse);

    return float4(color, 1.0);
}
