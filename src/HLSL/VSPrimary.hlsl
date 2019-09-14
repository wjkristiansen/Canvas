#include "Common.hlsli"

struct Vertex
{
    float3 Position;
    float3 Normal;
    float2 UV;
};

cbuffer Cb0 : register(b0)
{
    matrix<float, 3, 4> World[4];
    matrix<float, 4, 4> ViewProj;
}

StructuredBuffer<Vertex> Vertices : register(t0);

float3 TransformVec3x3(matrix<float, 3, 4> m, float3 v)
{
    return float3(
        dot(m[0].xyz, v),
        dot(m[1].xyz, v),
        dot(m[2].xyz, v)
        );
}

float4 TransformVec4x3(matrix<float, 3, 4> m, float4 v)
{
    return float4(
        dot(m[0], v),
        dot(m[1], v),
        dot(m[2], v),
        1.0
        );
}

float4 TransformVec4x4(matrix<float, 4, 4> m, float4 v)
{
    return float4(
        dot(m[0], v),
        dot(m[1], v),
        dot(m[2], v),
        dot(m[3], v)
        );
}

static float3 TriangleVerts[] =
{
    float3(1, 0, 0),
    float3(-1, -1, 0),
    float3(-1, 1, 0)
};

PS_INPUT VSPrimary(VS_INPUT Input)
{
    PS_INPUT Output;
    
//    Output.Normal = TransformVec3x3(World[0], Vertices[Input.VertexId].Normal);
//    float4 WorldPos = TransformVec4x3(World[0], float4(Vertices[Input.VertexId].Position, 1.0));
//    Output.WorldPosition = WorldPos.xyz;
//    Output.ClipPosition = TransformVec4x4(ViewProj, WorldPos);

    float3 WorldPos = TriangleVerts[Input.VertexId];
    Output.WorldPosition = WorldPos;
    Output.Normal = float3(0, 0, 1);
    Output.ClipPosition = TransformVec4x4(ViewProj, float4(WorldPos, 1));

    return Output;
}