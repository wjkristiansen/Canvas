//================================================================================================
// LightBVHTest -- correctness tests for per-light influence culling.
//
// Tests run against the real CanvasCore factory (XCanvas, XLight,
// XSceneNode) and the engine-loaded CanvasGfx12 plugin (only
// because XWorld construction needs an XGfxDevice).  No GPU work is
// dispatched; we just exercise the LightBVH classification and
// frustum-query logic against a hand-built scene graph.
//================================================================================================

#include "pch.h"
#include "LightBVH.h"

#include <algorithm>

using namespace Canvas;
using namespace Canvas::Math;

namespace CanvasUnitTest
{

namespace
{

// Create a point light at `position` with `range`, attach it under
// the given parent, and return both the light and its node.  Light
// is left in the default-enabled state.
void MakePointLight(XCanvas* canvas,
                    XSceneNode* parent,
                    const FloatVector4& position,
                    float range,
                    Gem::TGemPtr<XLight>& outLight,
                    Gem::TGemPtr<XSceneNode>& outNode)
{
    EXPECT_TRUE(Succeeded(canvas->CreateLight(LightType::Point, &outLight)));
    outLight->SetRange(range);

    EXPECT_TRUE(Succeeded(canvas->CreateSceneNode(&outNode)));
    outNode->SetLocalTranslation(position);
    EXPECT_TRUE(Succeeded(outNode->BindElement(outLight)));
    EXPECT_TRUE(Succeeded(parent->AddChild(outNode)));
}

// Forward = +X by convention (Canvas row-vector basis: row 0).  Spot
// light starts aimed along world +X from its position.
void MakeSpotLight(XCanvas* canvas,
                   XSceneNode* parent,
                   const FloatVector4& position,
                   float range,
                   float outerAngleDeg,
                   Gem::TGemPtr<XLight>& outLight,
                   Gem::TGemPtr<XSceneNode>& outNode)
{
    EXPECT_TRUE(Succeeded(canvas->CreateLight(LightType::Spot, &outLight)));
    outLight->SetRange(range);
    const float kDegToRad = 3.14159265f / 180.0f;
    outLight->SetSpotAngles(outerAngleDeg * 0.5f * kDegToRad, outerAngleDeg * kDegToRad);

    EXPECT_TRUE(Succeeded(canvas->CreateSceneNode(&outNode)));
    outNode->SetLocalTranslation(position);
    // Identity rotation -- spot aims along the node's +X basis row.
    EXPECT_TRUE(Succeeded(outNode->BindElement(outLight)));
    EXPECT_TRUE(Succeeded(parent->AddChild(outNode)));
}

void MakeDirectionalLight(XCanvas* canvas,
                          XSceneNode* parent,
                          Gem::TGemPtr<XLight>& outLight,
                          Gem::TGemPtr<XSceneNode>& outNode)
{
    EXPECT_TRUE(Succeeded(canvas->CreateLight(LightType::Directional, &outLight)));
    EXPECT_TRUE(Succeeded(canvas->CreateSceneNode(&outNode)));
    EXPECT_TRUE(Succeeded(outNode->BindElement(outLight)));
    EXPECT_TRUE(Succeeded(parent->AddChild(outNode)));
}

void MakeAmbientLight(XCanvas* canvas,
                      XSceneNode* parent,
                      Gem::TGemPtr<XLight>& outLight,
                      Gem::TGemPtr<XSceneNode>& outNode)
{
    EXPECT_TRUE(Succeeded(canvas->CreateLight(LightType::Ambient, &outLight)));
    EXPECT_TRUE(Succeeded(canvas->CreateSceneNode(&outNode)));
    EXPECT_TRUE(Succeeded(outNode->BindElement(outLight)));
    EXPECT_TRUE(Succeeded(parent->AddChild(outNode)));
}

// Build a row-vector view*proj for a camera at `eye` looking down +X.
// Forward = +X, side = +Y, up = +Z (Canvas convention).  Reverse-Z
// projection so it matches Frustum::FromViewProjection's default.
FloatMatrix4x4 BuildViewProjLookingAlongX(const FloatVector4& eye,
                                          float fovY, float aspect,
                                          float zn, float zf)
{
    FloatMatrix4x4 view = {};
    // basis: forward=+X, side=+Y, up=+Z stored as columns of view
    view.M[0][0] = 0.0f; view.M[0][1] = 0.0f; view.M[0][2] = 1.0f; view.M[0][3] = 0.0f; // world X -> view forward (+Z)
    view.M[1][0] = 1.0f; view.M[1][1] = 0.0f; view.M[1][2] = 0.0f; view.M[1][3] = 0.0f; // world Y -> view side (+X)
    view.M[2][0] = 0.0f; view.M[2][1] = 1.0f; view.M[2][2] = 0.0f; view.M[2][3] = 0.0f; // world Z -> view up (+Y)
    view.M[3][0] = -eye.V[1];
    view.M[3][1] = -eye.V[2];
    view.M[3][2] = -eye.V[0];
    view.M[3][3] = 1.0f;

    FloatMatrix4x4 proj = PerspectiveReverseZ<float>(fovY, aspect, zn, zf);
    return view * proj;
}

bool ListContains(const std::vector<XLight*>& v, XLight* p)
{
    return std::find(v.begin(), v.end(), p) != v.end();
}

} // anonymous namespace

TEST(LightBVHTest, EmptyWorldBuildsCleanly)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    SceneChangeTracker tracker;
    LightBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    EXPECT_TRUE(bvh.IsBuilt());
    EXPECT_EQ(size_t(0), bvh.TrackedLightCount());
    EXPECT_EQ(size_t(0), bvh.UntrackedLightCount());

    std::vector<XLight*> visible;
    Frustum f = Frustum::FromViewProjection(
        PerspectiveReverseZ<float>(1.0f, 1.0f, 1.0f, 100.0f), true);
    bvh.QueryFrustum(f, visible);
    EXPECT_EQ(size_t(0), visible.size());
}

// Directional / Ambient lights have no spatial bound; they must
// land in the untracked list and never appear in QueryFrustum
// output.  This is the contract that lets the renderer treat them
// as always-visible without inflating the BVH.
TEST(LightBVHTest, NonSpatialLightsAreUntracked)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    Gem::TGemPtr<XLight> pDir, pAmb;
    Gem::TGemPtr<XSceneNode> pDirNode, pAmbNode;
    MakeDirectionalLight(pCanvas, pWorld->GetRootNode(), pDir, pDirNode);
    MakeAmbientLight   (pCanvas, pWorld->GetRootNode(), pAmb, pAmbNode);

    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    SceneChangeTracker tracker;
    LightBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    EXPECT_EQ(size_t(0), bvh.TrackedLightCount());
    EXPECT_EQ(size_t(2), bvh.UntrackedLightCount());
    EXPECT_FALSE(bvh.IsTrackedLight(pDir.Get()));
    EXPECT_FALSE(bvh.IsTrackedLight(pAmb.Get()));

    EXPECT_TRUE(ListContains(bvh.GetUntrackedLights(), pDir.Get()));
    EXPECT_TRUE(ListContains(bvh.GetUntrackedLights(), pAmb.Get()));

    // Untracked lights never come out of the frustum query (by design).
    std::vector<XLight*> visible;
    Frustum f = Frustum::FromViewProjection(
        PerspectiveReverseZ<float>(1.0f, 1.0f, 1.0f, 100.0f), true);
    bvh.QueryFrustum(f, visible);
    EXPECT_EQ(size_t(0), visible.size());
}

TEST(LightBVHTest, PointLightsAreTrackedAndCulled)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // Three point lights:
    //   pInFront -- inside the camera frustum
    //   pSide    -- well outside the FOV
    //   pBehind  -- behind the camera
    Gem::TGemPtr<XLight> pInFront, pSide, pBehind;
    Gem::TGemPtr<XSceneNode> pInFrontNode, pSideNode, pBehindNode;
    MakePointLight(pCanvas, pWorld->GetRootNode(),
                   FloatVector4(10.0f, 0.0f, 0.0f, 1.0f), 1.0f, pInFront, pInFrontNode);
    MakePointLight(pCanvas, pWorld->GetRootNode(),
                   FloatVector4(10.0f, 50.0f, 0.0f, 1.0f), 1.0f, pSide,    pSideNode);
    MakePointLight(pCanvas, pWorld->GetRootNode(),
                   FloatVector4(-10.0f, 0.0f, 0.0f, 1.0f), 1.0f, pBehind,  pBehindNode);

    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    SceneChangeTracker tracker;
    LightBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    EXPECT_EQ(size_t(3), bvh.TrackedLightCount());
    EXPECT_EQ(size_t(0), bvh.UntrackedLightCount());
    EXPECT_TRUE(bvh.IsTrackedLight(pInFront.Get()));
    EXPECT_TRUE(bvh.IsTrackedLight(pSide.Get()));
    EXPECT_TRUE(bvh.IsTrackedLight(pBehind.Get()));

    // Camera at origin looking down +X, 60deg FOV, near 0.5, far 100.
    FloatMatrix4x4 vp = BuildViewProjLookingAlongX(
        FloatVector4(0.0f, 0.0f, 0.0f, 1.0f),
        float(3.14159 / 3), 1.0f, 0.5f, 100.0f);
    Frustum f = Frustum::FromViewProjection(vp, true);

    std::vector<XLight*> visible;
    bvh.QueryFrustum(f, visible);

    EXPECT_TRUE(ListContains(visible, pInFront.Get())) << "In-front point light must be visible";
    EXPECT_FALSE(ListContains(visible, pSide.Get())) << "Side-offset point light must be culled";
    EXPECT_FALSE(ListContains(visible, pBehind.Get())) << "Behind-camera point light must be culled";
}

TEST(LightBVHTest, SpotLightsAreTracked)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    Gem::TGemPtr<XLight> pSpot;
    Gem::TGemPtr<XSceneNode> pSpotNode;
    MakeSpotLight(pCanvas, pWorld->GetRootNode(),
                  FloatVector4(5.0f, 0.0f, 0.0f, 1.0f),
                  10.0f, /*outerDeg*/ 30.0f,
                  pSpot, pSpotNode);

    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    SceneChangeTracker tracker;
    LightBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    EXPECT_EQ(size_t(1), bvh.TrackedLightCount());
    EXPECT_TRUE(bvh.IsTrackedLight(pSpot.Get()));

    // Camera at origin looking down +X sees the spot apex.
    FloatMatrix4x4 vp = BuildViewProjLookingAlongX(
        FloatVector4(0.0f, 0.0f, 0.0f, 1.0f),
        float(3.14159 / 3), 1.0f, 0.5f, 100.0f);
    Frustum f = Frustum::FromViewProjection(vp, true);

    std::vector<XLight*> visible;
    bvh.QueryFrustum(f, visible);
    EXPECT_TRUE(ListContains(visible, pSpot.Get()));
}

// A spot aimed along -(1,1,1)/sqrt(3) from the origin with range 10
// and a 40 degree half-angle aims downward, yet its rim rises above
// the apex to y = 10 * (sin40 * sqrt(2/3) - cos40 * sqrt(1/3)) ~= 0.83.
// The influence AABB must reach that far (sampling the rim at only a
// few points around a tilted axis misses that extreme) and should not
// reach much past it.
TEST(LightBVHTest, TiltedSpotBoundCoversLitRegion)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    Gem::TGemPtr<XLight> pSpot;
    Gem::TGemPtr<XSceneNode> pSpotNode;
    MakeSpotLight(pCanvas, pWorld->GetRootNode(),
                  FloatVector4(0.0f, 0.0f, 0.0f, 1.0f),
                  10.0f, /*outerDeg*/ 80.0f,
                  pSpot, pSpotNode);

    // Orthonormal basis with row 0 (the spot axis) = -(1,1,1)/sqrt(3).
    const float s3 = 1.0f / std::sqrt(3.0f);
    const float s2 = 1.0f / std::sqrt(2.0f);
    const float s6 = 1.0f / std::sqrt(6.0f);
    FloatMatrix4x4 basis = {};
    basis.M[0][0] = -s3;      basis.M[0][1] = -s3;      basis.M[0][2] = -s3;
    basis.M[1][0] = -s2;      basis.M[1][1] =  s2;      basis.M[1][2] = 0.0f;
    basis.M[2][0] =  s6;      basis.M[2][1] =  s6;      basis.M[2][2] = -2.0f * s6;
    basis.M[3][3] = 1.0f;
    pSpotNode->SetLocalRotation(QuaternionFromRotationMatrix(basis));

    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    const FloatMatrix4x4 world = pSpotNode->GetGlobalMatrix();
    ASSERT_NEAR(-s3, world[0][0], 1e-4f);
    ASSERT_NEAR(-s3, world[0][1], 1e-4f);
    ASSERT_NEAR(-s3, world[0][2], 1e-4f);

    SceneChangeTracker tracker;
    LightBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    ASSERT_TRUE(bvh.IsTrackedLight(pSpot.Get()));

    // Frustum reduced to the single half-space y >= yMin; the other
    // planes accept everything.
    auto halfSpaceAboveY = [](float yMin) {
        Frustum f;
        for (auto& p : f.Planes)
            p = FloatVector4(0.0f, 0.0f, 0.0f, 1.0f);
        f.Planes[Frustum::Bottom] = FloatVector4(0.0f, 1.0f, 0.0f, -yMin);
        return f;
    };

    std::vector<XLight*> visible;
    bvh.QueryFrustum(halfSpaceAboveY(0.5f), visible);
    EXPECT_TRUE(ListContains(visible, pSpot.Get()))
        << "Lit region reaches y ~= 0.83; bound must not be culled at y >= 0.5";

    bvh.QueryFrustum(halfSpaceAboveY(1.2f), visible);
    EXPECT_FALSE(ListContains(visible, pSpot.Get()))
        << "Lit region ends at y ~= 0.83; bound should be tight enough to cull at y >= 1.2";
}

// The renderer lights a spot's wider cone when its inner angle exceeds
// its outer angle, so the influence bound must cover the wider cone too.
// A spot at the origin aimed along +X with range 10 and a 170 degree
// cone reaches y = 10 * sin(85) ~= 9.96; the 20 degree cone reaches
// only y = 10 * sin(10) ~= 1.74.
TEST(LightBVHTest, SwappedSpotAnglesBoundTheWiderCone)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    Gem::TGemPtr<XLight> pSpot;
    Gem::TGemPtr<XSceneNode> pSpotNode;
    MakeSpotLight(pCanvas, pWorld->GetRootNode(),
                  FloatVector4(0.0f, 0.0f, 0.0f, 1.0f),
                  10.0f, /*outerDeg*/ 20.0f,
                  pSpot, pSpotNode);
    const float kDegToRad = PiF / 180.0f;
    pSpot->SetSpotAngles(170.0f * kDegToRad, 20.0f * kDegToRad);

    SceneChangeTracker tracker;
    LightBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    ASSERT_TRUE(bvh.IsTrackedLight(pSpot.Get()));

    // Frustum reduced to the single half-space y >= 5.
    Frustum f;
    for (auto& p : f.Planes)
        p = FloatVector4(0.0f, 0.0f, 0.0f, 1.0f);
    f.Planes[Frustum::Bottom] = FloatVector4(0.0f, 1.0f, 0.0f, -5.0f);

    std::vector<XLight*> visible;
    bvh.QueryFrustum(f, visible);
    EXPECT_TRUE(ListContains(visible, pSpot.Get()))
        << "The wider cone reaches y ~= 9.96; the bound must not be culled at y >= 5";
}

// Rebuild must drop every primitive / tracking record from a prior
// build before consuming the new scene.  Defends against state
// accumulation when scene topology changes between Build calls.
TEST(LightBVHTest, RebuildClearsPriorState)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    Gem::TGemPtr<XLight> pPt;
    Gem::TGemPtr<XSceneNode> pPtNode;
    MakePointLight(pCanvas, pWorld->GetRootNode(),
                   FloatVector4(1.0f, 0.0f, 0.0f, 1.0f), 1.0f, pPt, pPtNode);

    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    SceneChangeTracker tracker;
    LightBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    EXPECT_EQ(size_t(1), bvh.TrackedLightCount());
    XLight* pPtRaw = pPt.Get();
    EXPECT_TRUE(bvh.IsTrackedLight(pPtRaw));

    // Rebuild against an empty root.
    Gem::TGemPtr<XSceneNode> pEmpty;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pEmpty)));
    bvh.Build(pEmpty);
    EXPECT_EQ(size_t(0), bvh.TrackedLightCount());
    EXPECT_EQ(size_t(0), bvh.UntrackedLightCount());
    EXPECT_FALSE(bvh.IsTrackedLight(pPtRaw));
}

// A light that moves after Build must be culled where it is now:
// Refit carries a point light out of the frustum and a spot light into
// it.
TEST(LightBVHTest, RefitFollowsMovedLights)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    Gem::TGemPtr<XLight> pPoint, pSpot;
    Gem::TGemPtr<XSceneNode> pPointNode, pSpotNode;
    MakePointLight(pCanvas, pWorld->GetRootNode(),
                   FloatVector4(10.0f, 0.0f, 0.0f, 1.0f), 1.0f, pPoint, pPointNode);
    MakeSpotLight(pCanvas, pWorld->GetRootNode(),
                  FloatVector4(-20.0f, 0.0f, 0.0f, 1.0f), 5.0f, /*outerDeg*/ 30.0f,
                  pSpot, pSpotNode);

    SceneChangeTracker tracker;
    LightBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());

    FloatMatrix4x4 vp = BuildViewProjLookingAlongX(
        FloatVector4(0.0f, 0.0f, 0.0f, 1.0f),
        PiF / 3.0f, 1.0f, 0.5f, 100.0f);
    Frustum f = Frustum::FromViewProjection(vp, true);

    std::vector<XLight*> visible;
    bvh.QueryFrustum(f, visible);
    EXPECT_TRUE(ListContains(visible, pPoint.Get()));
    EXPECT_FALSE(ListContains(visible, pSpot.Get()));

    pPointNode->SetLocalTranslation(FloatVector4(-10.0f, 0.0f, 0.0f, 1.0f));
    pSpotNode->SetLocalTranslation(FloatVector4(20.0f, 0.0f, 0.0f, 1.0f));
    bvh.Refit();
    tracker.ClearChanges();

    bvh.QueryFrustum(f, visible);
    EXPECT_FALSE(ListContains(visible, pPoint.Get())) << "Point light moved behind the camera";
    EXPECT_TRUE(ListContains(visible, pSpot.Get())) << "Spot light moved in front of the camera";

    // Growing the range brings the moved point light's influence back
    // into view.
    pPoint->SetRange(15.0f);
    bvh.Refit();
    tracker.ClearChanges();
    bvh.QueryFrustum(f, visible);
    EXPECT_TRUE(ListContains(visible, pPoint.Get()));
}

// A point light's range moving to or from zero changes whether it has
// a spatial bound; Refit must move it between the tracked and
// untracked lists.
TEST(LightBVHTest, RefitReclassifiesZeroRangeLights)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    Gem::TGemPtr<XLight> pPoint;
    Gem::TGemPtr<XSceneNode> pPointNode;
    MakePointLight(pCanvas, pWorld->GetRootNode(),
                   FloatVector4(10.0f, 0.0f, 0.0f, 1.0f), 0.0f, pPoint, pPointNode);

    SceneChangeTracker tracker;
    LightBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    EXPECT_FALSE(bvh.IsTrackedLight(pPoint.Get()));
    EXPECT_TRUE(ListContains(bvh.GetUntrackedLights(), pPoint.Get()));

    pPoint->SetRange(2.0f);
    bvh.Refit();
    tracker.ClearChanges();
    EXPECT_TRUE(bvh.IsTrackedLight(pPoint.Get()));
    EXPECT_FALSE(ListContains(bvh.GetUntrackedLights(), pPoint.Get()));

    pPoint->SetRange(0.0f);
    bvh.Refit();
    tracker.ClearChanges();
    EXPECT_FALSE(bvh.IsTrackedLight(pPoint.Get()));
    EXPECT_TRUE(ListContains(bvh.GetUntrackedLights(), pPoint.Get()));
}

} // namespace CanvasUnitTest
