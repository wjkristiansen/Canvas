#include "Common.hlsli"

// Separate structured buffers matching CMeshData12 layout (float4 per element)
StructuredBuffer<float4> Positions : register(t0);
StructuredBuffer<float4> Normals : register(t1);

PS_INPUT VSPrimary(VS_INPUT Input)
{
    PS_INPUT Output;

    float3 pos = Positions[Input.VertexId].xyz;
    float3 nrm = Normals[Input.VertexId].xyz;

    // Transform position to world space (row-vector: pos * World)
    float4 worldPos = mul(float4(pos, 1.0), World);
    Output.WorldPosition = worldPos.xyz;

    // Transform normal to world space using inverse-transpose
    Output.Normal = normalize(mul(float4(nrm, 0.0), WorldInvTranspose).xyz);

    // Transform to clip space (row-vector: worldPos * ViewProj)
    Output.ClipPosition = mul(worldPos, ViewProj);

    return Output;
}