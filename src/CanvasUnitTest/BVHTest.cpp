//================================================================================================
// BVHTest -- keystone correctness test for the scene BVH.
//
// Contract: the BVH must produce EXACTLY the same visible set as a
// brute-force linear scan that applies Frustum::IntersectsAABB to every
// primitive.  False negatives (silently hidden geometry) are never
// allowed.  False positives are bounded by the conservative p-vertex
// test that brute force uses too, so the two sets are equal -- not
// merely "BVH is a superset."
//
// This test fuzzes that property across many random scenes and
// frustums, plus explicit corner cases.  Any failure is a real bug in
// the BVH builder or traversal.
//================================================================================================

#include "pch.h"
#include "CppUnitTest.h"

#include "BVH.h"

#include <algorithm>
#include <random>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Canvas;
using namespace Canvas::Math;

namespace CanvasUnitTest
{

namespace
{

BVHPrimitive MakePointPrimitive(float x, float y, float z, uint32_t id)
{
    BVHPrimitive prim;
    prim.WorldBounds = AABB(FloatVector4(x, y, z, 0.0f), FloatVector4(x, y, z, 0.0f));
    prim.Id = id;
    prim.Node = nullptr;
    prim.Flags = BVHPrimitiveFlag_Static;
    return prim;
}

BVHPrimitive MakeBoxPrimitive(const FloatVector4& mn, const FloatVector4& mx, uint32_t id)
{
    BVHPrimitive prim;
    prim.WorldBounds = AABB(mn, mx);
    prim.Id = id;
    prim.Node = nullptr;
    prim.Flags = BVHPrimitiveFlag_Static;
    return prim;
}

// Brute-force reference: returns the indices (into primitives) of every
// primitive whose bounds pass the frustum test, in ascending order.
std::vector<uint32_t> BruteForceCull(const std::vector<BVHPrimitive>& primitives, const Frustum& f)
{
    std::vector<uint32_t> out;
    out.reserve(primitives.size());
    for (uint32_t i = 0; i < primitives.size(); ++i)
        if (f.IntersectsAABB(primitives[i].WorldBounds))
            out.push_back(i);
    return out;
}

std::vector<uint32_t> SortedCopy(std::vector<uint32_t> v)
{
    std::sort(v.begin(), v.end());
    return v;
}

// Build a perspective view-proj from a camera at `eye` looking at `target`
// with worldUp = +Y.  Returns a row-vector view*proj suitable for
// Frustum::FromViewProjection(reverseZ=true).
//
// View basis (Canvas view space is LHS, +X right, +Y up, +Z forward):
//   forward = normalize(target - eye)
//   side    = normalize(up x forward)
//   up      = forward x side
FloatMatrix4x4 BuildViewProj(const FloatVector4& eye, const FloatVector4& target,
                             float fovY, float aspect, float zn, float zf)
{
    float fx = target.V[0] - eye.V[0];
    float fy = target.V[1] - eye.V[1];
    float fz = target.V[2] - eye.V[2];
    float flen = std::sqrt(fx*fx + fy*fy + fz*fz);
    if (flen < 1e-6f) { fx = 0; fy = 0; fz = 1; flen = 1; }
    fx /= flen; fy /= flen; fz /= flen;

    // side = (0,1,0) x forward = (fz, 0, -fx)
    float sx = fz;
    float sy = 0.0f;
    float sz = -fx;
    float slen = std::sqrt(sx*sx + sy*sy + sz*sz);
    if (slen < 1e-6f) { sx = 1; sy = 0; sz = 0; slen = 1; }
    sx /= slen; sy /= slen; sz /= slen;

    // up = forward x side
    const float ux = fy * sz - fz * sy;
    const float uy = fz * sx - fx * sz;
    const float uz = fx * sy - fy * sx;

    // Row-vector world->view matrix: basis vectors as columns,
    // translation is -dot(eye, basis_row) per row.
    FloatMatrix4x4 view = {};
    view.M[0][0] = sx;  view.M[0][1] = ux;  view.M[0][2] = fx;  view.M[0][3] = 0.0f;
    view.M[1][0] = sy;  view.M[1][1] = uy;  view.M[1][2] = fy;  view.M[1][3] = 0.0f;
    view.M[2][0] = sz;  view.M[2][1] = uz;  view.M[2][2] = fz;  view.M[2][3] = 0.0f;
    view.M[3][0] = -(eye.V[0]*sx + eye.V[1]*sy + eye.V[2]*sz);
    view.M[3][1] = -(eye.V[0]*ux + eye.V[1]*uy + eye.V[2]*uz);
    view.M[3][2] = -(eye.V[0]*fx + eye.V[1]*fy + eye.V[2]*fz);
    view.M[3][3] = 1.0f;

    FloatMatrix4x4 proj = PerspectiveReverseZ<float>(fovY, aspect, zn, zf);
    return view * proj;
}

} // anonymous namespace

TEST_CLASS(BVHTest)
{
public:

    // Empty BVH must produce empty cull set without crashing or accessing
    // out-of-bounds memory.  Defends the early-out in QueryFrustum.
    TEST_METHOD(EmptyBVH)
    {
        BVH bvh;
        bvh.Build(nullptr, 0);
        Assert::IsTrue(bvh.Empty());

        Frustum f = Frustum::FromViewProjection(
            PerspectiveReverseZ<float>(1.0f, 1.0f, 1.0f, 100.0f), true);
        std::vector<uint32_t> visible;
        bvh.QueryFrustum(nullptr, f, visible);
        Assert::AreEqual(size_t(0), visible.size());
    }

    // All-empty primitives (sentinel AABBs) must be silently dropped at
    // build time.  The BVH should report Empty() even though count > 0.
    TEST_METHOD(EmptySentinelPrimitivesAreDropped)
    {
        std::vector<BVHPrimitive> primitives;
        BVHPrimitive a; a.WorldBounds = AABB{}; a.Id = 0;
        BVHPrimitive b; b.WorldBounds = AABB{}; b.Id = 1;
        primitives.push_back(a);
        primitives.push_back(b);
        BVH bvh;
        bvh.Build(primitives.data(), primitives.size());
        Assert::IsTrue(bvh.Empty());
    }

    // Single-leaf-node tree: trivial path through Build, exercises the
    // count <= kMaxPrimitivesPerLeaf early-out.
    TEST_METHOD(SingleLeafNode)
    {
        std::vector<BVHPrimitive> primitives;
        for (uint32_t i = 0; i < 3; ++i)
            primitives.push_back(MakePointPrimitive(0.0f, 0.0f, 10.0f + float(i), i));
        BVH bvh;
        bvh.Build(primitives.data(), primitives.size());
        Assert::IsFalse(bvh.Empty());
        Assert::AreEqual(size_t(3), bvh.PrimitiveCount());

        Frustum f = Frustum::FromViewProjection(
            PerspectiveReverseZ<float>(float(3.14159 / 2), 1.0f, 0.5f, 100.0f), true);
        std::vector<uint32_t> visible;
        bvh.QueryFrustum(primitives.data(), f, visible);
        Assert::AreEqual(size_t(3), visible.size());
    }

    // Brute-force equivalence on a hand-built scene: ensures the
    // dedup/ordering of BVH output matches the reference modulo sort.
    TEST_METHOD(BruteForceEquivalenceHandBuilt)
    {
        std::vector<BVHPrimitive> primitives;
        for (uint32_t i = 0; i < 50; ++i)
        {
            const float x = -20.0f + float(i % 10) * 4.0f;
            const float y = -10.0f + float((i / 10) % 5) * 4.0f;
            const float z = 5.0f + float(i % 7) * 3.0f;
            primitives.push_back(MakeBoxPrimitive(
                FloatVector4(x - 0.5f, y - 0.5f, z - 0.5f, 0.0f),
                FloatVector4(x + 0.5f, y + 0.5f, z + 0.5f, 0.0f),
                i));
        }
        BVH bvh;
        bvh.Build(primitives.data(), primitives.size());

        FloatMatrix4x4 vp = BuildViewProj(
            FloatVector4(0.0f, 0.0f, 0.0f, 1.0f),
            FloatVector4(0.0f, 0.0f, 1.0f, 1.0f),
            float(3.14159 / 3), 16.0f / 9.0f, 0.5f, 100.0f);
        Frustum f = Frustum::FromViewProjection(vp, true);

        std::vector<uint32_t> bvhVisible;
        bvh.QueryFrustum(primitives.data(), f, bvhVisible);
        std::vector<uint32_t> bfVisible = BruteForceCull(primitives, f);

        Assert::AreEqual(bfVisible.size(), bvhVisible.size(),
                         L"BVH visible count != brute force");
        auto bvhSorted = SortedCopy(bvhVisible);
        auto bfSorted  = SortedCopy(bfVisible);
        for (size_t i = 0; i < bfSorted.size(); ++i)
            Assert::AreEqual(bfSorted[i], bvhSorted[i]);
    }

    // The keystone fuzz: many random scenes, many random cameras, every
    // pairing must agree exactly with brute force.  Seeded RNG so any
    // failure is reproducible from the test log alone.
    TEST_METHOD(BruteForceEquivalenceFuzz)
    {
        std::mt19937 rng(0xC0FFEE);
        std::uniform_real_distribution<float> posDist(-50.0f, 50.0f);
        std::uniform_real_distribution<float> halfDist(0.25f, 2.5f);
        std::uniform_real_distribution<float> camDist(-30.0f, 30.0f);
        std::uniform_real_distribution<float> tgtDist(-30.0f, 30.0f);
        std::uniform_real_distribution<float> fovDist(0.5f, 2.0f);   // ~28 to 115 deg
        std::uniform_real_distribution<float> aspDist(0.5f, 2.5f);

        const int kPrimitiveCount = 500;
        const int kFrustumCount = 25;

        std::vector<BVHPrimitive> primitives;
        primitives.reserve(kPrimitiveCount);
        for (int i = 0; i < kPrimitiveCount; ++i)
        {
            const float cx = posDist(rng), cy = posDist(rng), cz = posDist(rng);
            const float hx = halfDist(rng), hy = halfDist(rng), hz = halfDist(rng);
            primitives.push_back(MakeBoxPrimitive(
                FloatVector4(cx - hx, cy - hy, cz - hz, 0.0f),
                FloatVector4(cx + hx, cy + hy, cz + hz, 0.0f),
                uint32_t(i)));
        }

        BVH bvh;
        bvh.Build(primitives.data(), primitives.size());
        Assert::AreEqual(size_t(kPrimitiveCount), bvh.PrimitiveCount());

        for (int t = 0; t < kFrustumCount; ++t)
        {
            FloatVector4 eye(camDist(rng), camDist(rng), camDist(rng), 1.0f);
            FloatVector4 tgt(tgtDist(rng), tgtDist(rng), tgtDist(rng), 1.0f);
            // Avoid degenerate eye==target.
            if (std::abs(eye.V[0]-tgt.V[0]) + std::abs(eye.V[1]-tgt.V[1]) + std::abs(eye.V[2]-tgt.V[2]) < 0.1f)
                tgt.V[2] += 5.0f;

            FloatMatrix4x4 vp = BuildViewProj(eye, tgt, fovDist(rng), aspDist(rng), 0.5f, 200.0f);
            Frustum f = Frustum::FromViewProjection(vp, true);

            std::vector<uint32_t> bvhVisible;
            bvh.QueryFrustum(primitives.data(), f, bvhVisible);
            std::vector<uint32_t> bfVisible = BruteForceCull(primitives, f);

            auto bvhSorted = SortedCopy(bvhVisible);
            auto bfSorted  = SortedCopy(bfVisible);

            // Detailed assertion: report sizes on the first mismatch so
            // the failure log is actionable.
            if (bvhSorted != bfSorted)
            {
                std::wstringstream ss;
                ss << L"Mismatch on frustum #" << t
                   << L"  bvh.size=" << bvhSorted.size()
                   << L"  bf.size=" << bfSorted.size();
                Assert::Fail(ss.str().c_str());
            }
        }
    }

    // All-outside: a frustum looking away from any primitive must return
    // empty.  Catches plane-sign inversions that would falsely include
    // everything.
    TEST_METHOD(FrustumLookingAway)
    {
        std::vector<BVHPrimitive> primitives;
        for (uint32_t i = 0; i < 20; ++i)
            primitives.push_back(MakePointPrimitive(float(i), 0.0f, 50.0f, i));
        BVH bvh;
        bvh.Build(primitives.data(), primitives.size());

        // Camera at origin looking down -Z (away from all primitives
        // which sit at z=50, i.e. behind the camera in this view).
        FloatMatrix4x4 vp = BuildViewProj(
            FloatVector4(0,0,0,1), FloatVector4(0,0,-1,1),
            float(3.14159 / 3), 1.0f, 0.5f, 100.0f);
        Frustum f = Frustum::FromViewProjection(vp, true);

        std::vector<uint32_t> visible;
        bvh.QueryFrustum(primitives.data(), f, visible);
        Assert::AreEqual(size_t(0), visible.size());
    }
};

} // namespace CanvasUnitTest
