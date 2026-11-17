//================================================================================================
// WorldBVH
//
// Owns the primitive table for a CWorld and the BVH built over it.
//
//   1. Walk a scene-graph subtree on demand (Build) to extract a flat
//      list of BVHPrimitive, one per element of every node that has an
//      element reporting a non-empty GetLocalBounds().
//   2. Hand that primitive list to BVH for SAH-binned construction.
//   3. Keep each primitive's world bounds current (Refit) by updating
//      only the nodes the world's SceneChangeTracker reports as moved,
//      as having an element bound, or as having an element change its
//      bounds, so a node the BVH tracks is culled where it is now.
//   4. Service per-frame frustum queries, deduplicating primitives back
//      to their owning XSceneNode* so the caller can filter
//      SubmitForRender at node granularity.
//
// Refit cannot follow structure changes -- nodes added or removed, or
// elements bound -- so the caller rebuilds after one (CWorld does, from
// SceneChangeTracker::HasStructureChanged).
//
// This class does NOT:
//   - own ray-query state for hybrid RT
//================================================================================================

#pragma once

#include "pch.h"
#include "BVH.h"
#include "SceneChangeTracker.h"

namespace Canvas
{

struct XCamera;
struct XSceneNode;

class WorldBVH
{
public:
    // `tracker` reports the changes Refit applies, and must outlive
    // this WorldBVH.
    explicit WorldBVH(SceneChangeTracker& tracker) : m_Tracker(tracker) {}
    WorldBVH(const WorldBVH&) = delete;
    WorldBVH& operator=(const WorldBVH&) = delete;
    ~WorldBVH() { Clear(); }

    // Walks the subtree rooted at `root`, extracts renderable
    // primitives, and builds the BVH.  Discards any prior state first;
    // safe to call repeatedly when scene graph topology changes.  Null root
    // is a no-op (leaves IsBuilt() == false).
    //
    // Primitive criterion: a node is renderable iff at least one of
    // its elements reports non-empty GetLocalBounds(), and each element
    // of a renderable node contributes one primitive.  Lights, cameras,
    // and other non-renderable elements honor this contract by
    // returning the empty sentinel (see XSceneElement::GetLocalBounds),
    // so nodes carrying only those contribute nothing, with no per-type
    // branching here.  Empty primitives stay out of the tree until
    // their bounds become non-empty.
    //
    // Tracks every renderable node with the tracker, and holds a
    // reference on it, so Refit can read a node even after it leaves
    // the graph and before the caller's next Build drops it.
    CANVAS_API void Build(XSceneNode* root);

    // Bring the BVH up to date with the renderable nodes the tracker
    // reports as changed: recompute their primitives' world bounds,
    // then refit the BVH over them.  Requires that the scene's
    // structure has not changed since Build (see the class comment).
    // Call once per frame before querying, and before the tracker's
    // owner clears its changes.  No-op when not built.
    CANVAS_API void Refit();

    // Makes the next Refit recompute every primitive's world bounds, not
    // just those of the nodes the tracker reports.  For changes no node
    // reports, such as a material's displacement inflating the bounds of
    // every mesh using it (see XGfxDevice::GetMeshBoundsGeneration).
    void InvalidateAllBounds() { m_AllBoundsStale = true; }

    // Reset to the pre-Build state.  Frees BVH/primitive memory,
    // untracks the renderable nodes, and releases the held nodes.
    CANVAS_API void Clear();

    bool IsBuilt() const { return m_Built; }
    size_t PrimitiveCount() const { return m_Primitives.size(); }
    size_t RenderableNodeCount() const { return m_RenderableNodeList.size(); }

    // True iff `node` had at least one renderable element at the last
    // successful Build.  A node added after Build returns false, so a
    // caller that has not rebuilt yet passes it through unculled rather
    // than hiding it.
    CANVAS_API bool IsRenderableNode(XSceneNode* node) const;

    // Compute the visible-node set for the given camera's view frustum.
    // The output set is cleared first.  Multiple primitives belonging
    // to the same node collapse to a single entry, matching the
    // granularity at which RenderQueue::SubmitForRender consumes.
    // Empty / unbuilt BVH produces an empty set without error.
    CANVAS_API void QueryFrustum(XCamera* camera,
                                 std::unordered_set<XSceneNode*>& outVisibleNodes) const;

    // Direct frustum query (no camera dereference) for callers that
    // already have a Frustum in hand (tests, future shadow passes).
    CANVAS_API void QueryFrustum(const Math::Frustum& frustum,
                                 std::unordered_set<XSceneNode*>& outVisibleNodes) const;

private:
    // A node that contributes primitives, one per bound element.  Its
    // primitives are m_Primitives[FirstPrimitive, FirstPrimitive +
    // PrimitiveCount), in binding order.
    struct RenderableNode
    {
        Gem::TGemPtr<XSceneNode> Node;
        uint32_t FirstPrimitive = 0;
        uint32_t PrimitiveCount = 0;
    };

    SceneChangeTracker& m_Tracker;
    std::vector<BVHPrimitive> m_Primitives;

    // The references that keep every renderable node alive.  The raw
    // XSceneNode* in m_RenderableNodes and in each BVHPrimitive::Node
    // point into these, and are filled and emptied with them.
    std::vector<RenderableNode> m_RenderableNodeList;

    // Index into m_RenderableNodeList of each renderable node.
    std::unordered_map<XSceneNode*, uint32_t> m_RenderableNodes;

    BVH m_BVH;

    // Scratch space reused across QueryFrustum calls to avoid per-frame
    // allocation; mutable so const queries can populate it.
    mutable std::vector<uint32_t> m_VisiblePrimitiveScratch;

    bool m_Built = false;
    bool m_AllBoundsStale = false;
};

} // namespace Canvas
