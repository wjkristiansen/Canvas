#include "Common.hlsli"

// Separate structured buffers matching CMeshData12 layout
StructuredBuffer<float4> Positions : register(t0);  // root SRV
StructuredBuffer<float4> Normals   : register(t1);
StructuredBuffer<float2> UV0       : register(t2);
StructuredBuffer<float4> Tangents  : register(t3);  // xyz = T, w = bitangent sign
StructuredBuffer<uint4>  BoneIndices : register(t10); // 4 bone indices per vertex
StructuredBuffer<float4> BoneWeights : register(t11); // 4 bone weights per vertex
StructuredBuffer<float4x4> BoneMatrices : register(t12); // palette; bone i = InvBind[i] * boneWorldMatrix[i]

PS_INPUT VSPrimary(VS_INPUT Input)
{
    PS_INPUT Output;

    float3 pos = Positions[Input.VertexId].xyz;
    float3 nrm = Normals[Input.VertexId].xyz;

    float3 worldPos3;
    float3 worldNrm;

    if ((PerObject.MaterialFlags & MATERIAL_FLAG_HAS_SKIN) != 0)
    {
        // Linear Blend Skinning - bone matrices already encode world space,
        // so PerObject.World is not applied for skinned vertices.
        uint4  bi = BoneIndices[Input.VertexId];
        float4 bw = BoneWeights[Input.VertexId];
        float3 sp = float3(0.0f, 0.0f, 0.0f);
        float3 sn = float3(0.0f, 0.0f, 0.0f);
        [unroll] for (int k = 0; k < 4; ++k)
        {
            float4x4 M = BoneMatrices[bi[k]];
            sp += bw[k] * mul(float4(pos, 1.0f), M).xyz;
            sn += bw[k] * mul(float4(nrm, 0.0f), M).xyz;
        }
        worldPos3 = sp;
        worldNrm  = normalize(sn);
    }
    else
    {
        float4 wp = mul(float4(pos, 1.0f), PerObject.World);
        worldPos3 = wp.xyz;
        worldNrm  = normalize(mul(float4(nrm, 0.0f), PerObject.WorldInvTranspose).xyz);
    }

    Output.WorldPosition = worldPos3;
    Output.Normal        = worldNrm;
    Output.ClipPosition  = mul(float4(worldPos3, 1.0f), PerFrame.ViewProj);

    // UV0 — uniform branch on HAS_UV. SRV is always bound (null-safe), but
    // skip the load when no real stream is present.
    if ((PerObject.MaterialFlags & MATERIAL_FLAG_HAS_UV) != 0)
        Output.TexCoord = UV0[Input.VertexId];
    else
        Output.TexCoord = float2(0.0f, 0.0f);

    // Tangent — same pattern; also blend tangent with bone matrices when skinned.
    if ((PerObject.MaterialFlags & MATERIAL_FLAG_HAS_TANGENT) != 0)
    {
        float4 tan = Tangents[Input.VertexId];
        float3 tanDir;
        if ((PerObject.MaterialFlags & MATERIAL_FLAG_HAS_SKIN) != 0)
        {
            uint4  bi = BoneIndices[Input.VertexId];
            float4 bw = BoneWeights[Input.VertexId];
            float3 st = float3(0.0f, 0.0f, 0.0f);
            [unroll] for (int k = 0; k < 4; ++k)
                st += bw[k] * mul(float4(tan.xyz, 0.0f), BoneMatrices[bi[k]]).xyz;
            tanDir = normalize(st);
        }
        else
        {
            tanDir = normalize(mul(float4(tan.xyz, 0.0f), PerObject.World).xyz);
        }
        Output.Tangent = float4(tanDir, tan.w);
    }
    else
    {
        Output.Tangent = float4(1.0f, 0.0f, 0.0f, 1.0f);
    }

    return Output;
}
