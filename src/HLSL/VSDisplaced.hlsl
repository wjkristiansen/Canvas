// VSDisplaced.hlsl - vertex stage for the engine's displaced-mesh path.
//
// Reads per-CP position, UV, and base normal from StructuredBuffer SRVs
// (no IA bindings) and applies the per-tile world transform.  The DS
// displaces the bilerped CP position along the bilerped normal by the
// displacement-map sample, so heightfields (flat XY base, normal=+Z) are
// the degenerate case of a general curved-base displacement.

#include "Displaced.hlsli"

DisplacedControlPoint VSDisplaced(uint vertexId : SV_VertexID)
{
    float4 localPos = Positions[vertexId];
    float2 uv       = UV0s     [vertexId];
    float3 localN   = Normals  [vertexId].xyz;

    float4 worldPos = mul(localPos, PerTile.World);

    float3 worldN = normalize(mul(float4(localN, 0.0), PerTile.WorldInvTranspose).xyz);

    DisplacedControlPoint cp;
    cp.TileUV   = uv;
    cp.WorldPos = worldPos.xyz;
    cp.Normal   = worldN;
    return cp;
}