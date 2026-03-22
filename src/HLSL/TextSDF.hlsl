//================================================================================================
// TextSDF.hlsl - Text Rendering with SDF Sampling
//
// Vertex and pixel shaders for SDF-based text rendering.
// Samples signed distance field texels and reconstructs sharp edges via smoothstep.
//================================================================================================

cbuffer PerFrameConstants : register(b0)
{
    row_major float4x4 ViewProj;        // Combined view-projection matrix
    float4 CameraWorldPos;              // Camera position (w unused)
    float4 AmbientLight;                // Ambient light color (w = intensity)
    float4 SunDirection;                // Direction toward sun (w unused)
    float4 SunColor;                    // Sun color * intensity (w unused)
};

cbuffer PerObjectConstants : register(b1)
{
    row_major float4x4 World;           // World transform matrix
    float4 Color;                       // Text color (w unused)
};

// SDF Atlas texture and sampler
Texture2D      SDFAtlas : register(t0);
SamplerState   SDFSampler : register(s0);

//------------------------------------------------------------------------------------------------
// Vertex Shader
//================================================================================================

struct VSInput
{
    float3 Position : POSITION;     // World/screen position
    float2 TexCoord : TEXCOORD0;    // Atlas UV coordinates
    uint   Color    : COLOR;        // Per-vertex color (RGBA packed)
};

struct VSOutput
{
    float4 Position : SV_POSITION;  // Clip-space position
    float2 TexCoord : TEXCOORD0;    // Atlas UVs (interpolated to PS)
    float4 Color    : TEXCOORD1;    // Unpacked color
};

VSOutput main(VSInput input)
{
    VSOutput output;
    
    // Transform position to world space
    float4 worldPos = float4(input.Position, 1.0f);
    worldPos = mul(worldPos, World);
    
    // Transform to clip space
    output.Position = mul(worldPos, ViewProj);
    
    // Pass through texture coordinates
    output.TexCoord = input.TexCoord;
    
    // Unpack color from uint32 RGBA
    output.Color = float4(
        ((input.Color >> 0) & 0xFF) / 255.0f,   // R
        ((input.Color >> 8) & 0xFF) / 255.0f,   // G
        ((input.Color >> 16) & 0xFF) / 255.0f,  // B
        ((input.Color >> 24) & 0xFF) / 255.0f   // A
    );
    
    return output;
}

//------------------------------------------------------------------------------------------------
// Pixel Shader - SDF Edge Reconstruction
//================================================================================================

struct PSOutput
{
    float4 Color : SV_TARGET;
};

PSOutput main(VSOutput input) : SV_TARGET
{
    // Sample SDF texture at interpolated coordinate
    float sdfValue = SDFAtlas.Sample(SDFSampler, input.TexCoord).r;
    
    // Convert from [0, 1] to signed distance
    // Range: 0 = far outside, 0.5 = edge, 1 = far inside
    float signedDist = (sdfValue - 0.5f) * 2.0f;
    
    // Reconstruct edge using smoothstep
    // Edge width (in SDF space, where 0.5 is edge and range is [-1, 1])
    float edgeWidth = 0.1f;
    float alpha = smoothstep(-edgeWidth, edgeWidth, signedDist);
    
    // Output: modulate input color by computed alpha
    PSOutput output;
    output.Color = float4(input.Color.rgb, input.Color.a * alpha);
    
    return output;
}

//================================================================================================
// Alternative: MSDF (Multi-channel SDF) Pixel Shader
//
// For higher-quality edge reconstruction using all three channels.
// Each channel represents distance to different edge features.
//================================================================================================

/*
// Uncomment for MSDF version (requires 3-channel SDF data)

float median(float a, float b, float c)
{
    return max(min(a, b), min(max(a, b), c));
}

PSOutput main_MSDF(VSOutput input) : SV_TARGET
{
    // Sample three channels from MSDF atlas
    float3 msd = SDFAtlas.Sample(SDFSampler, input.TexCoord).rgb;
    
    // Find median of the three signed distances
    float signedDist = median(msd.r, msd.g, msd.b);
    
    // Compute alpha from median signed distance
    float edgeWidth = 0.15f;
    float alpha = smoothstep(-edgeWidth, edgeWidth, signedDist - 0.5f);
    
    // Alternative: screen-space derivatives for adaptive edge width
    // float3 msd_dx = ddx(msd);
    // float3 msd_dy = ddy(msd);
    // float edgeWidth = length(float2(length(msd_dx), length(msd_dy))) * 0.5f;
    
    PSOutput output;
    output.Color = float4(input.Color.rgb, input.Color.a * alpha);
    
    return output;
}
*/
