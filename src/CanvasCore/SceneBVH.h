//================================================================================================
// SceneBVH
//
// Owns the primitive table for a CScene and the BVH built over it.
//
//   1. Walk a scene-graph subtree on demand (Build) to extract a flat
//      list of BVHPrimitive from every (node, element) pair whose
//      element reports a non-empty GetLocalBounds().
//   2. Hand that primitive list to BVH for SAH-binned construction.
//   3. Service per-frame frustum queries, deduplicating primitives back
//      to their owning XSceneGraphNode* so the caller can filter
//      SubmitForRender at node granularity.
//
// Today this class does NOT:
//   - track dirty transforms (no refit, no per-frame update)
//   - distinguish static from dynamic (all primitives treated as static)
//   - hold an influence-BVH for light culling
//   - own ray-query state for hybrid RT
//================================================================================================

#pragma once

#include "pch.h"
#include "BVH.h"

namespace Canvas
{

struct XCamera;
struct XSceneGraphNode;

class SceneBVH
{
public:
    SceneBVH() = default;

    // Walks the subtree rooted at `root`, extracts renderable
    // primitives, and builds the BVH.  Discards any prior state first;
    // safe to call repeatedly when scene topology changes.  Null root
    // is a no-op (leaves IsBuilt() == false).
    //
    // Primitive criterion: an element contributes a primitive iff
    // element->GetLocalBounds() is non-empty.  Lights, cameras, and
    // other non-renderable elements honor this contract by returning
    // the empty sentinel (see XSceneGraphElement::GetLocalBounds), so
    // they are skipped automatically with no per-type branching here.
    void Build(XSceneGraphNode* root);

    // Reset to the pre-Build state.  Frees BVH/primitive memory.
    void Clear();

    bool IsBuilt() const { return m_Built; }
    size_t PrimitiveCount() const { return m_Primitives.size(); }
    size_t RenderableNodeCount() const { return m_RenderableNodes.size(); }

    // True iff `node` had at least one renderable element at the last
    // successful Build.  Nodes added to the graph after Build return
    // false -- they will (correctly) bypass the visibility filter until
    // the next Build.  This is the right behavior for a static BVH:
    // a stale BVH never *hides* geometry, it only fails to cull it.
    bool IsRenderableNode(XSceneGraphNode* node) const;

    // Compute the visible-node set for the given camera's view frustum.
    // The output set is cleared first.  Multiple primitives belonging
    // to the same node collapse to a single entry, matching the
    // granularity at which RenderQueue::SubmitForRender consumes.
    // Empty / unbuilt BVH produces an empty set without error.
    void QueryFrustum(XCamera* camera,
                      std::unordered_set<XSceneGraphNode*>& outVisibleNodes) const;

    // Direct frustum query (no camera dereference) for callers that
    // already have a Frustum in hand (tests, future shadow passes).
    void QueryFrustum(const Math::Frustum& frustum,
                      std::unordered_set<XSceneGraphNode*>& outVisibleNodes) const;

private:
    std::vector<BVHPrimitive> m_Primitives;
    std::unordered_set<XSceneGraphNode*> m_RenderableNodes;
    BVH m_BVH;

    // Scratch space reused across QueryFrustum calls to avoid per-frame
    // allocation; mutable so const queries can populate it.
    mutable std::vector<uint32_t> m_VisiblePrimitiveScratch;

    bool m_Built = false;
};

} // namespace Canvas
