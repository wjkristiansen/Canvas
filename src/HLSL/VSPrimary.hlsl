#include "Common.hlsli"

// Separate structured buffers matching CMeshData12 layout
StructuredBuffer<float4> Positions : register(t0);  // root SRV
StructuredBuffer<float4> Normals   : register(t1);
StructuredBuffer<float2> UV0       : register(t2);
StructuredBuffer<float4> Tangents  : register(t3);  // xyz = T, w = bitangent sign
StructuredBuffer<uint4>  BoneIndices : register(t10); // 4 bone indices per vertex
StructuredBuffer<float4> BoneWeights : register(t11); // 4 bone weights per vertex
StructuredBuffer<float4x4> BoneMatrices : register(t12); // palette; bone i = InvBind[i] * boneWorldMatrix[i]

// Normal transform for the row-vector linear map L: the inverse-transpose up to a positive
// scale (rows are cofactors), which normalize() removes. The determinant's sign keeps
// normals facing outward under mirroring.
float3x3 NormalMatrix(float3x3 L)
{
    float3x3 c = float3x3(cross(L[1], L[2]), cross(L[2], L[0]), cross(L[0], L[1]));
    return dot(L[0], c[0]) < 0.0f ? -c : c;
}

PS_INPUT VSPrimary(VS_INPUT Input)
{
    PS_INPUT Output;

    float3 pos = Positions[Input.VertexId].xyz;
    float3 nrm = Normals[Input.VertexId].xyz;

    float3 worldPos3;
    float3 worldNrm;
    float4x4 skinMatrix = (float4x4)0;

    if ((PerObject.MaterialFlags & MATERIAL_FLAG_HAS_SKIN) != 0)
    {
        // Linear Blend Skinning - bone matrices already encode world space,
        // so PerObject.World is not applied for skinned vertices.
        uint4  bi = BoneIndices[Input.VertexId];
        float4 bw = BoneWeights[Input.VertexId];
        [unroll] for (int k = 0; k < 4; ++k)
            skinMatrix += bw[k] * BoneMatrices[bi[k]];
        worldPos3 = mul(float4(pos, 1.0f), skinMatrix).xyz;
        worldNrm  = normalize(mul(nrm, NormalMatrix((float3x3)skinMatrix)));
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

    // Tangent — same pattern; skinned tangents use the blended skin matrix.
    // A mirroring transform flips the bitangent the pixel shader rebuilds from
    // cross(N, T), so the handedness sign takes the determinant's sign.
    if ((PerObject.MaterialFlags & MATERIAL_FLAG_HAS_TANGENT) != 0)
    {
        float4 tan = Tangents[Input.VertexId];
        float3x3 L = ((PerObject.MaterialFlags & MATERIAL_FLAG_HAS_SKIN) != 0)
            ? (float3x3)skinMatrix
            : (float3x3)PerObject.World;
        float3 tanDir = normalize(mul(tan.xyz, L));
        float  detSign = dot(L[0], cross(L[1], L[2])) < 0.0f ? -1.0f : 1.0f;
        Output.Tangent = float4(tanDir, tan.w * detSign);
    }
    else
    {
        Output.Tangent = float4(1.0f, 0.0f, 0.0f, 1.0f);
    }

    return Output;
}
