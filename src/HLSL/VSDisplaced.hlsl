// VSDisplaced.hlsl - vertex stage for the engine's displaced-mesh path.
//
// No vertex buffer: control points are generated from SV_VertexID, given
// the per-instance patch grid dimensions. For a draw with PatchGridDim = N,
// the IA is invoked with 4 * N * N "vertices" using PATCH_4_CONTROL_POINT
// topology, so each successive 4 inputs form one quad patch.
//
// CP layout per patch (cornerIdx in [0..3], in tile-(u, v) parameter space):
//   0 -> (0, 0)     1 -> (1, 0)
//   3 -> (0, 1)     2 -> (1, 1)
//
// The CP carries:
//   - TileUV: [0, 1]^2 across the whole tile; DS uses this to sample the
//     heightmap directly (D3D texture sampling: v=0 is image top row).
//   - WorldXY0Z: world-space (x, y, 0) before the height lift in DS.
//
// Coordinate handedness reconciliation:
//   D3D texture (u, v) has v growing top-to-bottom; viewed normally,
//   cross(u_hat, v_hat) points INTO the image (away from the reader).
//   Canvas world has (worldX-fwd, worldY-left, worldZ-up) RHS; viewed
//   from above, cross(worldX_hat, worldY_hat) points UP (toward the
//   bird's-eye viewer).  These two "natural viewer normals" point in
//   opposite directions, so a straight identity-positive (u, v) ->
//   (worldX, worldY) mapping would render the terrain as a vertical
//   mirror of the source heightmap.  To eliminate that mirror the 2D
//   image-to-world Jacobian must have determinant -1.  Here we flip
//   the v -> worldY mapping (image top row, v=0, maps to worldY =
//   +worldSize.y/2 = Canvas-left, which is "up" in a north-up
//   bird's-eye view), giving det = -worldSize.x * worldSize.y < 0.
//   The matching normal sign in DSDisplaced.hlsl::ComputeWorldNormal
//   and the HSDisplaced.hlsl outputtopology (triangle_cw) keep the
//   resulting world geometry CCW-front in Canvas world, consistent
//   with the engine winding contract documented in
//   CanvasMath.hpp::PerspectiveReverseZ.

#include "Displaced.hlsli"

DisplacedControlPoint VSDisplaced(uint vertexId : SV_VertexID)
{
    const uint cps_per_patch = 4u;
    uint patchIndex = vertexId / cps_per_patch;
    uint cornerIdx  = vertexId % cps_per_patch;

    uint pgrid = PerTile.PatchGridDim;
    uint patchX = patchIndex % pgrid;
    uint patchY = patchIndex / pgrid;

    uint2 cornerOff = uint2(0, 0);
    if (cornerIdx == 1u) cornerOff = uint2(1u, 0u);
    if (cornerIdx == 2u) cornerOff = uint2(1u, 1u);
    if (cornerIdx == 3u) cornerOff = uint2(0u, 1u);

    // Tile-space (u, v) of this CP.  v stays in D3D texture orientation
    // (v=0 = image top) so heightmap sampling in DS uses TileUV directly.
    float2 cpTile = float2(patchX + cornerOff.x, patchY + cornerOff.y) / float(pgrid);

    // World-space XY.  worldX grows with u (image-left -> Canvas-back,
    // image-right -> Canvas-forward).  worldY uses (1 - v) so image-top
    // (v=0) maps to worldY = origin.y + worldSize.y (Canvas-left, which
    // is "up" in a north-up bird's-eye view).  See header comment.
    float2 worldOrigin = PerTile.TileOriginAndSize.xy;
    float2 worldSize   = PerTile.TileOriginAndSize.zw;
    float2 worldXY     = float2(
        worldOrigin.x + cpTile.x          * worldSize.x,
        worldOrigin.y + (1.0 - cpTile.y)  * worldSize.y);

    DisplacedControlPoint cp;
    cp.TileUV    = cpTile;
    cp.WorldXY0Z = float3(worldXY.x, worldXY.y, 0.0);
    return cp;
}