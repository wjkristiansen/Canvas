struct VS_INPUT
{
    uint VertexId : SV_VertexID;
    uint InstanceId : SV_InstanceID;
};

struct PS_INPUT
{
    float3 Normal : Normal;
    float3 WorldPosition : WorldPosition;
    float4 ClipPosition : SV_Position;
};

// Per-frame constant buffer (register b0, root CBV slot 0)
cbuffer PerFrameConstants : register(b0)
{
    row_major float4x4 ViewProj;        // Combined view-projection matrix (row-vector convention)
    float3 CameraWorldPos;              // Camera position in world space
    float _pad0;
    float3 AmbientLight;                // Ambient light color/intensity
    float _pad1;
    float3 SunDirection;                // Directional light direction (toward light)
    float _pad2;
    float3 SunColor;                    // Directional light color * intensity
    float _pad3;
};

// Per-object constant buffer (register b1, descriptor table)
cbuffer PerObjectConstants : register(b1)
{
    row_major float4x4 World;           // Object-to-world transform (row-vector convention)
    row_major float4x4 WorldInvTranspose; // For transforming normals
};

