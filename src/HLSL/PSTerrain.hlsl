// PSTerrain.hlsl - terrain pixel stage.
//
// v1 scaffold: writes the DS-supplied world position and normal into the
// deferred G-buffer alongside a placeholder albedo / PBR. The composite
// shader picks it up via the existing G-buffer sampling path, so no
// changes to PSComposite are required.
//
// The composite albedo / AO / roughness textures will be bound to t1/t2/t3
// in a follow-up; for the scaffold the terrain uses fixed factors.

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
    output.Normals      = float4(N * 0.5 + 0.5, 1.0);
    output.DiffuseColor = float4(0.45, 0.50, 0.35, 1.0);  // placeholder olive until v2 material binds
    output.WorldPos     = float4(input.WorldPos, 1.0);
    output.PBR          = float4(0.85, 0.0, 1.0, 0.0);    // R=rough, G=metal, B=AO, A=spare
    output.Emissive     = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
