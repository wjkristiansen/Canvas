//================================================================================================
// BVH
//
// CPU-side, single-threaded, static-only, SAH-binned top-down build over
// an array of BVHPrimitive entries.  Produces a binary tree of AABB
// nodes whose leaf nodes reference contiguous ranges of a reordered
// primitive-index array.  The BVHPrimitive array itself is owned
// externally (SceneBVH) and is NOT reordered by the BVH; only the
// BVH's internal index array is permuted, so caller-held
// BVHPrimitiveIds remain valid across rebuilds.
//
// Traversal: iterative depth-first with a fixed-size node stack.
// Frustum culling is the only supported query today.
//================================================================================================

#pragma once

#include "pch.h"

namespace Canvas
{

struct XSceneGraphNode;

// Reserved for future use.  The current build produces and consumes
// only None / Static.
enum BVHPrimitiveFlags : uint32_t
{
    BVHPrimitiveFlag_None         = 0,
    BVHPrimitiveFlag_Static       = 1u << 0,
    BVHPrimitiveFlag_Dynamic      = 1u << 1,
    BVHPrimitiveFlag_CastsShadow  = 1u << 2,
};

// Stable index into the SceneBVH-owned primitive table.
using BVHPrimitiveId = uint32_t;

constexpr BVHPrimitiveId kInvalidBVHPrimitiveId = UINT32_MAX;

// A BVHPrimitive is the BVH's user-level payload: pairs a world-space
// AABB with a back-pointer to the XSceneGraphNode that produced it.
// Primitives live in a flat array owned by SceneBVH.
struct BVHPrimitive
{
    // World-space AABB.  Currently set once at Build() time as
    // localBounds.Transform(node->GetGlobalMatrix())
    // and never updated.
    Math::AABB WorldBounds;

    // Weak back-pointer to the producing node.  Lifetime is guaranteed
    // by the scene graph holding a Gem reference to the node.
    XSceneGraphNode* Node = nullptr;

    BVHPrimitiveId Id = kInvalidBVHPrimitiveId;
    uint32_t Flags = BVHPrimitiveFlag_None;
};

struct BVHNode
{
    // Tight bounds over all primitives in this subtree.
    Math::AABB Bounds;

    // Inner node:  LeftOrFirst    = index of left child node; right = left + 1
    //                               (children are always stored contiguously,
    //                               so one index addresses both).
    //              PrimitiveCount = 0.
    // Leaf node:   LeftOrFirst    = index into BVH::GetPrimitiveIndices() of
    //                               the first primitive belonging to this node.
    //              PrimitiveCount > 0 = number of contiguous primitive entries.
    uint32_t LeftOrFirst    = 0;
    uint32_t PrimitiveCount = 0;

    bool IsLeaf() const { return PrimitiveCount > 0; }
};

class BVH
{
public:
    BVH() = default;

    // (Re)build the BVH from the supplied primitive array.  The
    // primitive array is read-only here; only the BVH's internal index
    // permutation is modified.  Empty input produces an empty BVH;
    // calling QueryFrustum on an empty BVH yields no results.
    // Primitives whose WorldBounds.IsEmpty() are silently skipped to
    // honor the AABB sentinel contract documented on AABB::IsEmpty().
    CANVAS_API void Build(const BVHPrimitive* primitives, size_t count);

    // Iterative frustum cull.  Appends to outVisiblePrimitiveIndices
    // the index (into the primitive array passed to Build) of every
    // primitive whose WorldBounds passes Frustum::IntersectsAABB.  The
    // output vector is NOT cleared first - callers typically reuse a
    // scratch vector across frames.  Result order is depth-first
    // traversal; stable per build but otherwise unspecified.
    //
    // `primitives` MUST point at the same array (or one with identical
    // bounds at the same indices) that was passed to Build; the BVH
    // stores only indices and needs the bounds back to perform
    // per-primitive retest at leaf nodes.  The retest is non-optional:
    // a leaf node's union bounds can pass the frustum even when none
    // of its individual primitives do, so skipping it produces
    // false-positive visibility.
    CANVAS_API void QueryFrustum(const BVHPrimitive* primitives,
                                 const Math::Frustum& frustum,
                                 std::vector<uint32_t>& outVisiblePrimitiveIndices) const;

    bool Empty() const { return m_Nodes.empty(); }
    size_t NodeCount() const { return m_Nodes.size(); }
    size_t PrimitiveCount() const { return m_PrimitiveIndices.size(); }
    const std::vector<BVHNode>& GetNodes() const { return m_Nodes; }
    const std::vector<uint32_t>& GetPrimitiveIndices() const { return m_PrimitiveIndices; }

private:
    // Recursive SAH binned split.  Operates on m_PrimitiveIndices in
    // place over [first, first+count); produces m_Nodes[nodeIndex] and
    // (when split) two child nodes appended at the back of m_Nodes.
    void BuildRecursive(const BVHPrimitive* primitives,
                        uint32_t nodeIndex,
                        uint32_t first,
                        uint32_t count);

    std::vector<BVHNode> m_Nodes;             // node 0 is root when non-empty
    std::vector<uint32_t> m_PrimitiveIndices; // permutation of [0, primitiveCount)
};

} // namespace Canvas
