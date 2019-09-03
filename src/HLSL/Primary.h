struct VS_INPUT
{
    uint VertexId : SV_VertexID;
    uint InstanceId : SV_InstanceID;
};

struct PS_INPUT
{
    uint PrimitiveId : SV_PrimitiveID;
    float3 Normal;
    float3 Position : SV_Position;
};

struct PS_OUTPUT
{
    uint RenderTargetIndex : SV_RenderTargetArrayIndex;
    float DiffuseColor : SV_Target0
    float3 Normal : SV_Target1
    float3 Position : SV_Target2
};

