#include "Common.hlsli"

PS_OUTPUT PSPrimary(PS_INPUT Input)
{
    PS_OUTPUT Output;

    Output.DiffuseColor = float3(1.0, 1.0, 1.0);
    Output.Normal = Input.Normal;
    Output.Position = Input.Position.xyz;

	return Output;
}