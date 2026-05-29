// HSDisplaced.hlsl - hull stage for the engine's displaced-mesh path.
//
// Patch-constant function computes per-edge tessellation factors from two
// signals:
//   1. Distance LOD - tessellate more for edges close to the camera.
//      Factor scales with edge_world_length / distance_to_edge_midpoint so
//      far-away patches collapse toward the minimum and near patches push
//      toward the maximum.
//
//   2. Curvature LOD - tessellate more where the displacement map has high
//      local 2nd derivative (ridges, peaks, valleys). Sampled from a
//      coarse mip so the signal is stable.
//
// Crack-free invariant: any quantity added to a shared edge's tess factor
// must be computable purely from the edge's geometry, so both neighboring
// patches arrive at bit-identical values. We use the edge midpoint (shared
// by both neighbors via shared corner CPs) for both the distance and the
// curvature sample. Quantities derived from the patch center (which
// neighbors do NOT share) can only be applied to the patch's inside tess
// factors, never to its edge factors.

#include "Displaced.hlsli"

static const float kMinTess = 2.0;
static const float kMaxTess = 32.0;

// Distance scale: larger -> more tessellation at a given distance.
static const float kDistanceLodScale = 10.0;

// Curvature signal weight. 2nd-derivative samples are in [0, 1] before
// MapScale; MapScale converts to world units of curvature per coarse-mip
// texel, then this constant maps those world units to tess-factor units.
static const float kCurvatureLodScale = 0.5;

// Coarse mip for the curvature read. Large enough to give a stable,
// blurred signal, small enough that meaningful curvature is still
// represented.
static const float kCurvatureMipLevel = 4.0;

// Displacement-map texel size in tile UV at the chosen curvature mip.
float CurvatureTexelUv()
{
    uint w, h, nMips;
    DisplacementMap.GetDimensions(0, w, h, nMips);
    return exp2(kCurvatureMipLevel) / float(max(w, h));
}

// 5-tap Laplacian curvature at a tile-UV point, returned in world units.
float CurvatureWorldUnitsAt(float2 uv)
{
    float texelUv = CurvatureTexelUv();
    float hC = DisplacementMap.SampleLevel(MapSampler, uv,                              kCurvatureMipLevel);
    float hL = DisplacementMap.SampleLevel(MapSampler, uv - float2(texelUv, 0), kCurvatureMipLevel);
    float hR = DisplacementMap.SampleLevel(MapSampler, uv + float2(texelUv, 0), kCurvatureMipLevel);
    float hD = DisplacementMap.SampleLevel(MapSampler, uv - float2(0, texelUv), kCurvatureMipLevel);
    float hU = DisplacementMap.SampleLevel(MapSampler, uv + float2(0, texelUv), kCurvatureMipLevel);
    float d2x = abs(hR + hL - 2.0 * hC);
    float d2y = abs(hU + hD - 2.0 * hC);
    return (d2x + d2y) * PerTile.MapScale;
}

// Per-edge factor combining distance and curvature, computed purely from
// edge-shared inputs (endpoints and their shared UVs). Both neighbors
// arrive at bit-identical results for a shared edge.
float EdgeTessFactor(DisplacedControlPoint a, DisplacedControlPoint b, float3 cam)
{
    float3 midpos = 0.5 * (a.WorldXY0Z + b.WorldXY0Z);
    float2 miduv  = 0.5 * (a.TileUV    + b.TileUV);

    float dist    = max(length(midpos - cam), 1.0);
    float edgeLen = length(b.WorldXY0Z - a.WorldXY0Z);

    float distanceFactor  = kDistanceLodScale * edgeLen / dist;
    float curvatureFactor = CurvatureWorldUnitsAt(miduv) * kCurvatureLodScale;

    return clamp(distanceFactor + curvatureFactor, kMinTess, kMaxTess);
}

DisplacedPatchConstants HSDisplacedPatchConst(InputPatch<DisplacedControlPoint, 4> patch)
{
    DisplacedPatchConstants pc;

    float3 cam = PerFrame.CameraWorldPos.xyz;

    // CP layout (from VS):
    //   0 -> (0,0)  1 -> (1,0)
    //   3 -> (0,1)  2 -> (1,1)
    //
    // D3D quad-domain edge convention:
    //   EdgeTess[0] = U=0 edge  (CP3 - CP0)   "left"
    //   EdgeTess[1] = V=0 edge  (CP1 - CP0)   "bottom"
    //   EdgeTess[2] = U=1 edge  (CP2 - CP1)   "right"
    //   EdgeTess[3] = V=1 edge  (CP2 - CP3)   "top"
    pc.EdgeTess[0] = EdgeTessFactor(patch[0], patch[3], cam);
    pc.EdgeTess[1] = EdgeTessFactor(patch[0], patch[1], cam);
    pc.EdgeTess[2] = EdgeTessFactor(patch[1], patch[2], cam);
    pc.EdgeTess[3] = EdgeTessFactor(patch[3], patch[2], cam);

    // Inside factors: average of the two opposing edges. These do not
    // cross patch boundaries, so per-patch quantities are fine here.
    pc.InsideTess[0] = 0.5 * (pc.EdgeTess[1] + pc.EdgeTess[3]);
    pc.InsideTess[1] = 0.5 * (pc.EdgeTess[0] + pc.EdgeTess[2]);

    return pc;
}

[domain("quad")]
[partitioning("integer")]
// VSDisplaced flips v -> worldY to eliminate the image-vs-bird's-eye
// mirror (see its header for the full reasoning).  That sign flip makes
// the 2D (u, v) -> (worldX, worldY) Jacobian determinant negative, which
// inverts the world-space winding of triangles emitted by the tessellator
// relative to the same topology on an identity-positive mapping.
//
// The tessellator emits one triangle whose vertices, in D3D's v-down
// domain (u-right, v-down), follow the topology winding.  Under v-down,
// the unsigned-area cross is the negation of the math (v-up) basis, so
// outputtopology = "triangle_ccw" actually emits triangles that are CW
// in the standard math basis.  Combined with the det-negative VSDisplaced
// mapping, those CW-in-math-domain triangles become CCW-front in Canvas
// world (right-hand normal +Z toward the sky), matching the engine
// winding contract.  The rasterizer then renders them via
// FrontCounterClockwise = TRUE (see CanvasGfx12 EnsureDefaultPSO /
// BuildDisplacedPSODesc).
[outputtopology("triangle_ccw")]
[outputcontrolpoints(4)]
[patchconstantfunc("HSDisplacedPatchConst")]
[maxtessfactor(64.0)]
DisplacedControlPoint HSDisplaced(
    InputPatch<DisplacedControlPoint, 4> patch,
    uint cpid : SV_OutputControlPointID,
    uint pid  : SV_PrimitiveID)
{
    return patch[cpid];
}