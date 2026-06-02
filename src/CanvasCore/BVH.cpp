//================================================================================================
// BVH implementation -- SAH binned top-down build + iterative frustum cull.
//================================================================================================

#include "pch.h"
#include "BVH.h"

namespace Canvas
{

namespace
{

// Surface area of an AABB.  Returns 0 for empty AABBs so the SAH cost
// model degenerates safely rather than producing negative or NaN costs.
float SurfaceArea(const Math::AABB& box)
{
    if (box.IsEmpty())
        return 0.0f;
    const float dx = box.Max.V[0] - box.Min.V[0];
    const float dy = box.Max.V[1] - box.Min.V[1];
    const float dz = box.Max.V[2] - box.Min.V[2];
    // Zero-extent on one or more axes is legitimate (e.g. a quad's
    // bounds collapsed to a plane) and produces a meaningful surface
    // area, so no clamping.
    return 2.0f * (dx * dy + dy * dz + dz * dx);
}

// Centroid of an AABB; partition key for SAH binning.  Ignores .w.
void Centroid(const Math::AABB& box, float out[3])
{
    out[0] = (box.Min.V[0] + box.Max.V[0]) * 0.5f;
    out[1] = (box.Min.V[1] + box.Max.V[1]) * 0.5f;
    out[2] = (box.Min.V[2] + box.Max.V[2]) * 0.5f;
}

constexpr uint32_t kSAHBinCount          = 16;   // standard balance of quality and cost
constexpr uint32_t kMaxPrimitivesPerLeaf = 4;    // small-leaf cutoff; below this an SAH split
                                                 // rarely beats keeping a leaf node
constexpr float    kTraversalCost        = 1.0f; // SAH constants in arbitrary units; only
constexpr float    kIntersectCost        = 1.0f; // ratios matter for split-vs-leaf decisions
constexpr uint32_t kMaxTraversalStack    = 64;   // BVH depth ~1.44*log2(N); 64 covers any
                                                 // plausible scene by a wide margin

} // anonymous namespace

void BVH::Build(const BVHPrimitive* primitives, size_t count)
{
    m_Nodes.clear();
    m_PrimitiveIndices.clear();

    // Skip empty-sentinel primitives.  Defending here means an upstream
    // caller that passes empties produces a smaller BVH rather than a
    // crash or a leaf node with a meaningless AABB.
    m_PrimitiveIndices.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!primitives[i].WorldBounds.IsEmpty())
            m_PrimitiveIndices.push_back(i);
    }

    if (m_PrimitiveIndices.empty())
        return;

    // Reserve worst-case node count (2N-1 for N leaf nodes).  Avoids
    // reallocations during recursion that would invalidate BVHNode
    // references we operate on.
    m_Nodes.reserve(2 * m_PrimitiveIndices.size());

    m_Nodes.emplace_back();
    BuildRecursive(primitives, /*nodeIndex*/ 0, /*first*/ 0,
                   /*count*/ static_cast<uint32_t>(m_PrimitiveIndices.size()));
}

void BVH::BuildRecursive(const BVHPrimitive* primitives,
                         uint32_t nodeIndex,
                         uint32_t first,
                         uint32_t count)
{
    // Compute this subtree's bounds and the centroid bounds used for
    // binning.  Centroid bounds are tighter than primitive bounds and
    // give the binner more separation when primitives overlap heavily.
    Math::AABB nodeBounds;
    Math::AABB centroidBounds;
    for (uint32_t i = 0; i < count; ++i)
    {
        const BVHPrimitive& prim = primitives[m_PrimitiveIndices[first + i]];
        nodeBounds.ExpandToInclude(prim.WorldBounds);
        float c[3];
        Centroid(prim.WorldBounds, c);
        centroidBounds.ExpandToInclude(Math::FloatVector4(c[0], c[1], c[2], 0.0f));
    }
    m_Nodes[nodeIndex].Bounds = nodeBounds;

    // Small-leaf early-out: skip binning for tiny subtrees.
    if (count <= kMaxPrimitivesPerLeaf)
    {
        m_Nodes[nodeIndex].LeftOrFirst    = first;
        m_Nodes[nodeIndex].PrimitiveCount = count;
        return;
    }

    // Pick the longest centroid axis to bin along.  Cheap and produces
    // near-optimal results in practice.
    const float ex = centroidBounds.Max.V[0] - centroidBounds.Min.V[0];
    const float ey = centroidBounds.Max.V[1] - centroidBounds.Min.V[1];
    const float ez = centroidBounds.Max.V[2] - centroidBounds.Min.V[2];
    int axis = 0;
    float extent = ex;
    if (ey > extent) { axis = 1; extent = ey; }
    if (ez > extent) { axis = 2; extent = ez; }

    // Degenerate: all centroids coincide.  Splitting yields no benefit;
    // force a leaf node rather than producing two identical children and
    // recursing forever.
    if (extent <= 0.0f)
    {
        m_Nodes[nodeIndex].LeftOrFirst    = first;
        m_Nodes[nodeIndex].PrimitiveCount = count;
        return;
    }

    // Bin the centroids along the chosen axis.
    struct Bin { Math::AABB Bounds; uint32_t Count = 0; };
    Bin bins[kSAHBinCount];

    const float axisMin = centroidBounds.Min.V[axis];
    const float binScale = static_cast<float>(kSAHBinCount) / extent;

    for (uint32_t i = 0; i < count; ++i)
    {
        const BVHPrimitive& prim = primitives[m_PrimitiveIndices[first + i]];
        float c[3];
        Centroid(prim.WorldBounds, c);
        // Clamp guards against floating-point landing exactly at the max
        // edge (which would produce binIdx == kSAHBinCount).
        uint32_t binIdx = static_cast<uint32_t>((c[axis] - axisMin) * binScale);
        if (binIdx >= kSAHBinCount) binIdx = kSAHBinCount - 1;
        bins[binIdx].Bounds.ExpandToInclude(prim.WorldBounds);
        bins[binIdx].Count++;
    }

    // Sweep bins to compute, for each candidate split between bin i and
    // bin i+1, the SAH cost = traversal + intersectCost *
    // (leftCount * leftSA + rightCount * rightSA) / parentSA.  We
    // precompute left/right prefix scans of bounds and counts so each
    // candidate is O(1).
    Math::AABB leftBounds [kSAHBinCount - 1];
    Math::AABB rightBounds[kSAHBinCount - 1];
    uint32_t   leftCount  [kSAHBinCount - 1] = {};
    uint32_t   rightCount [kSAHBinCount - 1] = {};

    Math::AABB accum;
    uint32_t   accumCount = 0;
    for (uint32_t i = 0; i < kSAHBinCount - 1; ++i)
    {
        accum.ExpandToInclude(bins[i].Bounds);
        accumCount += bins[i].Count;
        leftBounds[i] = accum;
        leftCount [i] = accumCount;
    }
    accum = Math::AABB{};
    accumCount = 0;
    for (int i = static_cast<int>(kSAHBinCount) - 1; i >= 1; --i)
    {
        accum.ExpandToInclude(bins[i].Bounds);
        accumCount += bins[i].Count;
        rightBounds[i - 1] = accum;
        rightCount [i - 1] = accumCount;
    }

    const float parentSA = SurfaceArea(nodeBounds);
    const float invParentSA = (parentSA > 0.0f) ? (1.0f / parentSA) : 0.0f;

    float bestCost = FLT_MAX;
    int   bestSplit = -1;
    for (uint32_t i = 0; i < kSAHBinCount - 1; ++i)
    {
        if (leftCount[i] == 0 || rightCount[i] == 0)
            continue; // an empty side is not a real split
        const float costL = SurfaceArea(leftBounds [i]) * static_cast<float>(leftCount [i]);
        const float costR = SurfaceArea(rightBounds[i]) * static_cast<float>(rightCount[i]);
        const float cost  = kTraversalCost + kIntersectCost * (costL + costR) * invParentSA;
        if (cost < bestCost)
        {
            bestCost  = cost;
            bestSplit = static_cast<int>(i);
        }
    }

    // Emit a leaf node if no split improves over keeping all primitives
    // here.  This is the SAH stopping criterion; without it the recursion
    // would happily keep subdividing well past the point of diminishing
    // returns.
    const float leafCost = kIntersectCost * static_cast<float>(count);
    if (bestSplit < 0 || bestCost >= leafCost)
    {
        m_Nodes[nodeIndex].LeftOrFirst    = first;
        m_Nodes[nodeIndex].PrimitiveCount = count;
        return;
    }

    // Partition m_PrimitiveIndices[first .. first+count) in place by bin
    // index <= bestSplit.  std::partition gives an O(count) in-place
    // reorder.
    const float splitPos = axisMin + (extent / static_cast<float>(kSAHBinCount))
                                   * static_cast<float>(bestSplit + 1);
    uint32_t* begin = m_PrimitiveIndices.data() + first;
    uint32_t* end   = begin + count;
    uint32_t* mid   = std::partition(begin, end, [&](uint32_t idx) {
        float c[3];
        Centroid(primitives[idx].WorldBounds, c);
        return c[axis] < splitPos;
    });
    const uint32_t leftCountFinal  = static_cast<uint32_t>(mid - begin);
    const uint32_t rightCountFinal = count - leftCountFinal;

    // Floating-point at the bin boundary can collapse one side to zero
    // even when the binner picked a valid split.  Fall back to a leaf
    // node in that case rather than infinite-recursing on a one-sided
    // partition.
    if (leftCountFinal == 0 || rightCountFinal == 0)
    {
        m_Nodes[nodeIndex].LeftOrFirst    = first;
        m_Nodes[nodeIndex].PrimitiveCount = count;
        return;
    }

    // Reserve two contiguous child slots and recurse.  Capture the left
    // child index BEFORE the emplace_backs because recursion may grow
    // m_Nodes further; LeftOrFirst stores only the left index since the
    // right child is always left+1.
    const uint32_t leftChildIndex = static_cast<uint32_t>(m_Nodes.size());
    m_Nodes.emplace_back();
    m_Nodes.emplace_back();

    m_Nodes[nodeIndex].LeftOrFirst    = leftChildIndex;
    m_Nodes[nodeIndex].PrimitiveCount = 0;

    BuildRecursive(primitives, leftChildIndex,     first,                  leftCountFinal);
    BuildRecursive(primitives, leftChildIndex + 1, first + leftCountFinal, rightCountFinal);
}

void BVH::QueryFrustum(const BVHPrimitive* primitives,
                       const Math::Frustum& frustum,
                       std::vector<uint32_t>& outVisiblePrimitiveIndices) const
{
    if (m_Nodes.empty())
        return;

    // Fixed-size stack: BVH depth grows like ~1.44*log2(N), so 64 covers
    // any plausible scene by a wide margin.  Overflow would corrupt
    // traversal; if it ever becomes possible, add an assert here and
    // grow the stack.
    uint32_t stack[kMaxTraversalStack];
    int top = 0;
    stack[top++] = 0; // root

    while (top > 0)
    {
        const uint32_t idx = stack[--top];
        const BVHNode& n = m_Nodes[idx];

        if (!frustum.IntersectsAABB(n.Bounds))
            continue;

        if (n.IsLeaf())
        {
            // Per-primitive retest: a leaf node's union bounds may pass
            // the frustum test even when individual primitives do not.
            // Skipping this step produces false-positive visibility.
            const uint32_t first = n.LeftOrFirst;
            const uint32_t end   = first + n.PrimitiveCount;
            for (uint32_t i = first; i < end; ++i)
            {
                const uint32_t primIdx = m_PrimitiveIndices[i];
                if (frustum.IntersectsAABB(primitives[primIdx].WorldBounds))
                    outVisiblePrimitiveIndices.push_back(primIdx);
            }
        }
        else
        {
            // Children are contiguous; push both.  No ordering preference
            // for frustum cull (unlike ray queries which want front-to-
            // back) -- the visibility set is unordered.
            const uint32_t left  = n.LeftOrFirst;
            const uint32_t right = left + 1;
            stack[top++] = left;
            stack[top++] = right;
        }
    }
}

} // namespace Canvas
