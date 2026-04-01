//================================================================================================
// PSRect.hlsl - Rectangle Rendering Pixel Shader
//
// Outputs the interpolated vertex color directly. No texture sampling.
//================================================================================================

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Color    : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET
{
    return input.Color;
}
