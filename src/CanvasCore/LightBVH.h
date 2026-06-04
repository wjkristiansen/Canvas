//================================================================================================
// LightBVH
//
// Owns the per-light primitive table for a CScene and the BVH built
// over it.  Parallel structure to SceneBVH, but the primitive AABBs
// describe each light's spatial *influence* (the volume within which
// the light can contribute >= cutoff radiance) rather than its mesh
// bounds.  This lets the renderer cull lights against the camera
// frustum so the per-frame HlslLight upload contains only lights
// that can affect visible pixels.
//
// Per-light influence AABB derivation:
//   - LightType::Point         bbox of Sphere(worldPos, Range)
//   - LightType::Spot          bbox of bounded Cone(apex, axisDir,
//                              Range, outerHalfAngle).  Computed from
//                              the cone's apex + four rim samples,
//                              which is loose but correct.
//   - LightType::Directional   not tracked (infinite extent).  Always
//                              passes the visibility filter.
//   - LightType::Ambient       not tracked (scene-global).  Always
//                              passes.
//   - LightType::Area          not tracked.  Always passes.
//
// This class does NOT:
//   - track dirty light transforms (no refit, no per-frame update)
//   - cull shadow casters (the shadow pass consumes the full caster
//     list)
//================================================================================================

#pragma once

#include "pch.h"
#include "BVH.h"

namespace Canvas
{

struct XCamera;
struct XLight;
struct XSceneGraphNode;

class LightBVH
{
public:
    LightBVH() = default;

    // Walks the subtree rooted at `root`, discovers every XLight bound
    // to any node via QueryInterface, computes a per-light influence
    // AABB, and builds the BVH.  Lights with no spatial bound
    // (directional / ambient / area) are recorded as "untracked" so
    // QueryFrustum-style consumers can mix them back in as always-pass
    // (see IsTrackedLight).  Discards prior state first; safe to call
    // repeatedly when scene topology changes.  Null root is a no-op
    // (leaves IsBuilt() == false).
    CANVAS_API void Build(XSceneGraphNode* root);

    // Reset to the pre-Build state.  Frees BVH / primitive memory.
    CANVAS_API void Clear();

    bool IsBuilt() const { return m_Built; }
    size_t TrackedLightCount() const { return m_LightForPrimitive.size(); }
    size_t UntrackedLightCount() const { return m_UntrackedLights.size(); }

    // True when `light` was seen at the last successful Build AND
    // contributes a spatial primitive (i.e. is subject to frustum
    // culling).  Lights of untracked type return false; consumers
    // wanting to include them must check via the UntrackedLights view
    // below or treat "not tracked" as "always visible".
    CANVAS_API bool IsTrackedLight(XLight* light) const;

    // Read-only view over the untracked-light table, populated at
    // Build time.  Renderer-side visibility consumers typically union
    // this with the QueryFrustum result before pushing to the GPU.
    const std::vector<XLight*>& GetUntrackedLights() const { return m_UntrackedLights; }

    // Compute the visible-light set for the given camera's frustum.
    // The output vector is cleared first.  Only TRACKED lights are
    // returned; untracked lights are out-of-scope here and must be
    // handled by the caller.  Duplicate primitives (a single light
    // contributes one primitive but the contract permits multiple)
    // are collapsed via the unordered_set scratch.
    // Empty / unbuilt BVH produces an empty vector without error.
    CANVAS_API void QueryFrustum(XCamera* camera, std::vector<XLight*>& outVisibleLights) const;

    // Direct frustum query (no camera dereference) for callers that
    // already have a Frustum in hand (e.g. unit tests).
    CANVAS_API void QueryFrustum(const Math::Frustum& frustum,
                                 std::vector<XLight*>& outVisibleLights) const;

private:
    // Per-tracked-light primitive table.  Index parallel to
    // m_LightForPrimitive: m_Primitives[i].Id == i, and that primitive
    // corresponds to m_LightForPrimitive[i].
    std::vector<BVHPrimitive> m_Primitives;
    std::vector<XLight*>      m_LightForPrimitive;

    // Untracked lights (directional / ambient / area / disabled),
    // captured so the caller can fold them back into the visible set.
    std::vector<XLight*>      m_UntrackedLights;

    // Lookup for IsTrackedLight.  Populated alongside m_Primitives so
    // O(1) membership without rescanning the primitive vector.
    std::unordered_set<XLight*> m_TrackedLightSet;

    BVH m_BVH;

    // Scratch reused across query calls to dedup primitive ids.
    mutable std::vector<uint32_t> m_VisiblePrimitiveScratch;

    bool m_Built = false;
};

} // namespace Canvas
