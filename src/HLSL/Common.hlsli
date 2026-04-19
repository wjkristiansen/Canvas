struct VS_INPUT
{
    uint VertexId : SV_VertexID;
    uint InstanceId : SV_InstanceID;
};

struct PS_INPUT
{
    float3 Normal : Normal;
    float3 WorldPosition : WorldPosition;
    float2 TexCoord : TexCoord0;
    float4 Tangent : Tangent;       // xyz = tangent, w = bitangent sign
    float4 ClipPosition : SV_Position;
};

// G-buffer MRT output from the geometry pass pixel shader
struct GBufferOutput
{
    float4 Normals      : SV_Target0;   // RGB = world-space normal (encoded), A = unused
    float4 DiffuseColor : SV_Target1;   // RGB = albedo, A = unused
    float4 WorldPos     : SV_Target2;   // RGB = world position, A = coverage
    float4 PBR          : SV_Target3;   // R = roughness, G = metallic, B = AO, A = unused
    float4 Emissive     : SV_Target4;   // RGB = linear emissive, A = unused
};

#include "HlslTypes.h"

// Per-frame constant buffer (register b0, root CBV slot 0)
ConstantBuffer<HlslPerFrameConstants> PerFrame : register(b0);

// Per-object constant buffer (register b1, descriptor table)
ConstantBuffer<HlslPerObjectConstants> PerObject : register(b1);

