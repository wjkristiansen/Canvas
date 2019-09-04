#include "Common.hlsli"

PS_INPUT VSPrimary(VS_INPUT Input)
{
    PS_INPUT Output;

    Output.Normal = float3(0.0, 1.0, 0.0);
    Output.Position = float4(0.0, 0.0, 0.0, 0.0);

    return Output;
}