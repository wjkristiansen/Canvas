//================================================================================================
// PSText.hlsl - Text Rendering Pixel Shader
//
// Samples the SDF glyph atlas at t1 (first SRV in the descriptor table).
// Reconstructs a sharp alpha edge from the signed distance value.
//================================================================================================

Texture2D    SDFAtlas    : register(t1);
SamplerState AtlasSampler : register(s0);

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : TEXCOORD1;
};

float4 main(VSOutput input) : SV_TARGET
{
    // SDF is stored as R8_UNorm: 0 = far outside, 0.5 = edge, 1 = far inside
    float sdf = SDFAtlas.Sample(AtlasSampler, input.TexCoord).r;

    // Map [0,1] to signed distance: -1 = far outside, 0 = edge, +1 = far inside
    float signedDist = (sdf - 0.5f) * 2.0f;

    // Anti-aliased edge: use screen-space derivative so the transition is always 1 pixel wide
    // regardless of glyph scale or font size (standard SDF technique).
    float edgeWidth = fwidth(signedDist);
    float alpha = smoothstep(-edgeWidth, edgeWidth, signedDist);

    return float4(input.Color.rgb, input.Color.a * alpha);
}
