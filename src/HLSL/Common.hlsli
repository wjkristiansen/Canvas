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

struct PS_OUTPUT
{
    float3 DiffuseColor : SV_Target0;
    float3 Normal : SV_Target1;
    float3 Position : SV_Target2;
};

