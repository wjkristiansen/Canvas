// VSTerrain.hlsl - terrain vertex stage.
//
// No vertex buffer: control points are generated from SV_VertexID, given the
// per-tile patch grid dimensions. For a tile with PatchGridDim = N, the IA is
// invoked with 4 * N * N "vertices" using PATCH_4_CONTROL_POINT topology, so
// each successive 4 inputs form one quad patch (CCW corners).
//
// CP layout per patch (cornerIdx in [0..3]):
//   0 -> (0, 0)     1 -> (1, 0)
//   3 -> (0, 1)     2 -> (1, 1)
//
// The CP carries:
//   - TileUV: [0, 1]^2 across the whole tile; DS uses this to sample the
//     heightmap directly.
//   - WorldXY0Z: world-space (x, y, 0) before the height lift in DS.

#include "Terrain.hlsli"

TerrainControlPoint VSTerrain(uint vertexId : SV_VertexID)
{
    const uint cps_per_patch = 4u;
    uint patchIndex = vertexId / cps_per_patch;
    uint cornerIdx  = vertexId % cps_per_patch;

    uint pgrid = PerTile.PatchGridDim;
    uint patchX = patchIndex % pgrid;
    uint patchY = patchIndex / pgrid;

    // CCW corner offsets within the patch.
    uint2 cornerOff = uint2(0, 0);
    if (cornerIdx == 1u) cornerOff = uint2(1u, 0u);
    if (cornerIdx == 2u) cornerOff = uint2(1u, 1u);
    if (cornerIdx == 3u) cornerOff = uint2(0u, 1u);

    // Tile-space normalized coordinates of this CP.
    float2 cpTile = float2(patchX + cornerOff.x, patchY + cornerOff.y) / float(pgrid);

    // World-space XY.
    float2 worldOrigin = PerTile.TileOriginAndSize.xy;
    float2 worldSize   = PerTile.TileOriginAndSize.zw;
    float2 worldXY = worldOrigin + cpTile * worldSize;

    TerrainControlPoint cp;
    cp.TileUV    = cpTile;
    cp.WorldXY0Z = float3(worldXY.x, worldXY.y, 0.0);
    return cp;
}
