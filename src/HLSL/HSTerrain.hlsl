// HSTerrain.hlsl - terrain hull stage.
//
// v1 scaffold: emits a constant tess factor of 8 on every edge and inside
// edge. Distance-based and curvature-based LOD will replace the constants
// in a follow-up (gpu-tess-shaders).
//
// Two HS entry points are mandatory in HLSL: a patch-constant function (run
// once per patch, emits tess factors) and a control-point function (run per
// output CP). Here the control-point function is a pass-through.

#include "Terrain.hlsli"

TerrainPatchConstants HSTerrainPatchConst(InputPatch<TerrainControlPoint, 4> patch)
{
    TerrainPatchConstants pc;
    const float kTess = 8.0;
    pc.EdgeTess[0]   = kTess;
    pc.EdgeTess[1]   = kTess;
    pc.EdgeTess[2]   = kTess;
    pc.EdgeTess[3]   = kTess;
    pc.InsideTess[0] = kTess;
    pc.InsideTess[1] = kTess;
    return pc;
}

[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("HSTerrainPatchConst")]
[maxtessfactor(64.0)]
TerrainControlPoint HSTerrain(
    InputPatch<TerrainControlPoint, 4> patch,
    uint cpid : SV_OutputControlPointID,
    uint pid  : SV_PrimitiveID)
{
    return patch[cpid];
}
