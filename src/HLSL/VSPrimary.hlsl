#include "Common.hlsli"

// Separate structured buffers matching CMeshData12 layout
StructuredBuffer<float4> Positions : register(t0);  // root SRV
StructuredBuffer<float4> Normals   : register(t1);
StructuredBuffer<float2> UV0       : register(t2);
StructuredBuffer<float4> Tangents  : register(t3);  // xyz = T, w = bitangent sign

PS_INPUT VSPrimary(VS_INPUT Input)
{
    PS_INPUT Output;

    float3 pos = Positions[Input.VertexId].xyz;
    float3 nrm = Normals[Input.VertexId].xyz;

    // Transform position to world space (row-vector: pos * World)
    float4 worldPos = mul(float4(pos, 1.0), PerObject.World);
    Output.WorldPosition = worldPos.xyz;

    // Transform normal to world space using inverse-transpose
    Output.Normal = normalize(mul(float4(nrm, 0.0), PerObject.WorldInvTranspose).xyz);

    // UV0 — uniform branch on HAS_UV. SRV is always bound (null-safe), but
    // skip the load when no real stream is present.
    if ((PerObject.MaterialFlags & MATERIAL_FLAG_HAS_UV) != 0)
        Output.TexCoord = UV0[Input.VertexId];
    else
        Output.TexCoord = float2(0.0, 0.0);

    // Tangent — same pattern.
    if ((PerObject.MaterialFlags & MATERIAL_FLAG_HAS_TANGENT) != 0)
    {
        float4 tan = Tangents[Input.VertexId];
        // Transform tangent direction by upper 3x3 of World (no translation).
        float3 tanWorld = normalize(mul(float4(tan.xyz, 0.0), PerObject.World).xyz);
        Output.Tangent = float4(tanWorld, tan.w);
    }
    else
    {
        Output.Tangent = float4(1.0, 0.0, 0.0, 1.0);
    }

    // Transform to clip space (row-vector: worldPos * ViewProj)
    Output.ClipPosition = mul(worldPos, PerFrame.ViewProj);

    return Output;
}