//================================================================================================
// LightBVH
//
// Owns the per-light primitive table for a CWorld and the BVH built
// over it.  Parallel structure to WorldBVH, but the primitive AABBs
// describe each light's spatial *influence* (the volume within which
// the light can contribute >= cutoff radiance) rather than its mesh
// bounds.  This lets the renderer cull lights against the camera
// frustum so the per-frame HlslLight upload contains only lights
// that can affect visible pixels.
//
// Per-light influence AABB derivation:
//   - LightType::Point         bbox of Sphere(worldPos, Range)
//   - LightType::Spot          exact bbox of the spherical sector
//                              (apex, axisDir, Range, outerHalfAngle):
//                              apex, rim circle, and any sphere
//                              extremes that fall inside the cone.
//   - LightType::Point, Range 0  not tracked.  Always passes.
//   - LightType::Directional   not tracked (infinite extent).  Always
//                              passes the visibility filter.
//   - LightType::Ambient       not tracked (scene-global).  Always
//                              passes.
//   - LightType::Area          not tracked.  Always passes.
//
// Refit keeps each influence AABB current by updating only the nodes
// the world's SceneChangeTracker reports as moved or as having a light
// change its range or spot angles.  It cannot follow structure changes
// -- nodes added or removed, or elements bound -- so the caller
// rebuilds after one (CWorld does, from
// SceneChangeTracker::HasStructureChanged); until then, a light added
// since Build is in neither list, and a consumer filtering by
// visibility drops it.
//
// This class does NOT:
//   - cull shadow casters (the shadow pass consumes the full caster
//     list)
//================================================================================================

#pragma once

#include "pch.h"
#include "BVH.h"
#include "SceneChangeTracker.h"

namespace Canvas
{

struct XCamera;
struct XLight;
struct XSceneNode;

class LightBVH
{
public:
    // `tracker` reports the changes Refit applies, and must outlive
    // this LightBVH.
    explicit LightBVH(SceneChangeTracker& tracker) : m_Tracker(tracker) {}
    LightBVH(const LightBVH&) = delete;
    LightBVH& operator=(const LightBVH&) = delete;
    ~LightBVH() { Clear(); }

    // Walks the subtree rooted at `root`, discovers every XLight bound
    // to any node via QueryInterface, computes a per-light influence
    // AABB, and builds the BVH.  Lights with no spatial bound
    // (directional / ambient / area) are recorded as "untracked" so
    // QueryFrustum-style consumers can mix them back in as always-pass
    // (see IsTrackedLight).  Discards prior state first; safe to call
    // repeatedly when scene topology changes.  Null root is a no-op
    // (leaves IsBuilt() == false).
    //
    // Tracks every node with a light bound with the tracker, and holds
    // a reference on it, on its lights and on `root`, so Refit can read
    // them even after they leave the graph.
    CANVAS_API void Build(XSceneNode* root);

    // Bring the influence AABBs up to date with the lit nodes the
    // tracker reports as changed: recompute their tracked lights'
    // bounds, then refit the BVH over them.  Rebuilds from the root
    // when one of their lights moves between tracked and untracked
    // (e.g. a point light whose range becomes zero).  Requires that the
    // scene's structure has not changed since Build (see the class
    // comment).  Call once per frame before querying, and before the
    // tracker's owner clears its changes.  No-op when not built.
    CANVAS_API void Refit();

    // Reset to the pre-Build state.  Frees BVH / primitive memory,
    // untracks the lit nodes, and releases the held nodes and lights.
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
    // A node with lights bound.  Its lights are
    // m_Lights[FirstLight, FirstLight + LightCount), in binding order.
    struct LitNode
    {
        Gem::TGemPtr<XSceneNode> Node;
        uint32_t FirstLight = 0;
        uint32_t LightCount = 0;
    };

    // A light, with the index of its primitive in m_Primitives, or
    // kInvalidBVHPrimitiveId when it is untracked.
    struct LightEntry
    {
        Gem::TGemPtr<XLight> Light;
        uint32_t PrimitiveIndex = kInvalidBVHPrimitiveId;
    };

    SceneChangeTracker& m_Tracker;
    Gem::TGemPtr<XSceneNode> m_pRoot;

    // The references that keep every lit node and every light alive.
    // The raw XSceneNode* and XLight* below all point into these, and
    // are filled and emptied with them.
    std::vector<LitNode>    m_LitNodeList;
    std::vector<LightEntry> m_Lights;

    // Index into m_LitNodeList of each lit node.
    std::unordered_map<XSceneNode*, uint32_t> m_LitNodes;

    // Per-tracked-light primitive table.  Index parallel to
    // m_LightForPrimitive: m_Primitives[i].Id == i, and that primitive
    // corresponds to m_LightForPrimitive[i].
    std::vector<BVHPrimitive> m_Primitives;
    std::vector<XLight*>      m_LightForPrimitive;

    // Untracked lights (directional / ambient / area / zero-range
    // point), captured so the caller can fold them back into the
    // visible set.
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
