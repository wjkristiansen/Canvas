//================================================================================================
// LightBVHTest -- correctness tests for per-light influence culling.
//
// Tests run against the real CanvasCore factory (XCanvas, XLight,
// XSceneGraphNode) and the engine-loaded CanvasGfx12 plugin (only
// because XScene construction needs an XGfxDevice).  No GPU work is
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
                    XSceneGraphNode* parent,
                    const FloatVector4& position,
                    float range,
                    Gem::TGemPtr<XLight>& outLight,
                    Gem::TGemPtr<XSceneGraphNode>& outNode)
{
    EXPECT_TRUE(Succeeded(canvas->CreateLight(LightType::Point, &outLight)));
    outLight->SetRange(range);

    EXPECT_TRUE(Succeeded(canvas->CreateSceneGraphNode(&outNode)));
    outNode->SetLocalTranslation(position);
    EXPECT_TRUE(Succeeded(outNode->BindElement(outLight)));
    EXPECT_TRUE(Succeeded(parent->AddChild(outNode)));
}

// Forward = +X by convention (Canvas row-vector basis: row 0).  Spot
// light starts aimed along world +X from its position.
void MakeSpotLight(XCanvas* canvas,
                   XSceneGraphNode* parent,
                   const FloatVector4& position,
                   float range,
                   float outerAngleDeg,
                   Gem::TGemPtr<XLight>& outLight,
                   Gem::TGemPtr<XSceneGraphNode>& outNode)
{
    EXPECT_TRUE(Succeeded(canvas->CreateLight(LightType::Spot, &outLight)));
    outLight->SetRange(range);
    const float kDegToRad = 3.14159265f / 180.0f;
    outLight->SetSpotAngles(outerAngleDeg * 0.5f * kDegToRad, outerAngleDeg * kDegToRad);

    EXPECT_TRUE(Succeeded(canvas->CreateSceneGraphNode(&outNode)));
    outNode->SetLocalTranslation(position);
    // Identity rotation -- spot aims along the node's +X basis row.
    EXPECT_TRUE(Succeeded(outNode->BindElement(outLight)));
    EXPECT_TRUE(Succeeded(parent->AddChild(outNode)));
}

void MakeDirectionalLight(XCanvas* canvas,
                          XSceneGraphNode* parent,
                          Gem::TGemPtr<XLight>& outLight,
                          Gem::TGemPtr<XSceneGraphNode>& outNode)
{
    EXPECT_TRUE(Succeeded(canvas->CreateLight(LightType::Directional, &outLight)));
    EXPECT_TRUE(Succeeded(canvas->CreateSceneGraphNode(&outNode)));
    EXPECT_TRUE(Succeeded(outNode->BindElement(outLight)));
    EXPECT_TRUE(Succeeded(parent->AddChild(outNode)));
}

void MakeAmbientLight(XCanvas* canvas,
                      XSceneGraphNode* parent,
                      Gem::TGemPtr<XLight>& outLight,
                      Gem::TGemPtr<XSceneGraphNode>& outNode)
{
    EXPECT_TRUE(Succeeded(canvas->CreateLight(LightType::Ambient, &outLight)));
    EXPECT_TRUE(Succeeded(canvas->CreateSceneGraphNode(&outNode)));
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
    // (row-vector: world->view multiply column-wise).
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

TEST(LightBVHTest, EmptySceneBuildsCleanly)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<XScene> pScene;
    EXPECT_TRUE(Succeeded(pCanvas->CreateScene(pDevice, &pScene)));

    LightBVH bvh;
    bvh.Build(pScene->GetRootNode());
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
    Gem::TGemPtr<XScene> pScene;
    EXPECT_TRUE(Succeeded(pCanvas->CreateScene(pDevice, &pScene)));

    Gem::TGemPtr<XLight> pDir, pAmb;
    Gem::TGemPtr<XSceneGraphNode> pDirNode, pAmbNode;
    MakeDirectionalLight(pCanvas, pScene->GetRootNode(), pDir, pDirNode);
    MakeAmbientLight   (pCanvas, pScene->GetRootNode(), pAmb, pAmbNode);

    EXPECT_TRUE(Succeeded(pScene->Update(0.0f)));

    LightBVH bvh;
    bvh.Build(pScene->GetRootNode());
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
    Gem::TGemPtr<XScene> pScene;
    EXPECT_TRUE(Succeeded(pCanvas->CreateScene(pDevice, &pScene)));

    // Three point lights:
    //   pInFront -- inside the camera frustum
    //   pSide    -- well outside the FOV
    //   pBehind  -- behind the camera
    Gem::TGemPtr<XLight> pInFront, pSide, pBehind;
    Gem::TGemPtr<XSceneGraphNode> pInFrontNode, pSideNode, pBehindNode;
    MakePointLight(pCanvas, pScene->GetRootNode(),
                   FloatVector4(10.0f, 0.0f, 0.0f, 1.0f), 1.0f, pInFront, pInFrontNode);
    MakePointLight(pCanvas, pScene->GetRootNode(),
                   FloatVector4(10.0f, 50.0f, 0.0f, 1.0f), 1.0f, pSide,    pSideNode);
    MakePointLight(pCanvas, pScene->GetRootNode(),
                   FloatVector4(-10.0f, 0.0f, 0.0f, 1.0f), 1.0f, pBehind,  pBehindNode);

    EXPECT_TRUE(Succeeded(pScene->Update(0.0f)));

    LightBVH bvh;
    bvh.Build(pScene->GetRootNode());
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
    Gem::TGemPtr<XScene> pScene;
    EXPECT_TRUE(Succeeded(pCanvas->CreateScene(pDevice, &pScene)));

    Gem::TGemPtr<XLight> pSpot;
    Gem::TGemPtr<XSceneGraphNode> pSpotNode;
    MakeSpotLight(pCanvas, pScene->GetRootNode(),
                  FloatVector4(5.0f, 0.0f, 0.0f, 1.0f),
                  10.0f, /*outerDeg*/ 30.0f,
                  pSpot, pSpotNode);

    EXPECT_TRUE(Succeeded(pScene->Update(0.0f)));

    LightBVH bvh;
    bvh.Build(pScene->GetRootNode());
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

// Rebuild must drop every primitive / tracking record from a prior
// build before consuming the new scene.  Defends against state
// accumulation when scene topology changes between Build calls.
TEST(LightBVHTest, RebuildClearsPriorState)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XScene> pScene;
    EXPECT_TRUE(Succeeded(pCanvas->CreateScene(pDevice, &pScene)));

    Gem::TGemPtr<XLight> pPt;
    Gem::TGemPtr<XSceneGraphNode> pPtNode;
    MakePointLight(pCanvas, pScene->GetRootNode(),
                   FloatVector4(1.0f, 0.0f, 0.0f, 1.0f), 1.0f, pPt, pPtNode);

    EXPECT_TRUE(Succeeded(pScene->Update(0.0f)));

    LightBVH bvh;
    bvh.Build(pScene->GetRootNode());
    EXPECT_EQ(size_t(1), bvh.TrackedLightCount());
    XLight* pPtRaw = pPt.Get();
    EXPECT_TRUE(bvh.IsTrackedLight(pPtRaw));

    // Rebuild against an empty root.
    Gem::TGemPtr<XSceneGraphNode> pEmpty;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pEmpty)));
    bvh.Build(pEmpty);
    EXPECT_EQ(size_t(0), bvh.TrackedLightCount());
    EXPECT_EQ(size_t(0), bvh.UntrackedLightCount());
    EXPECT_FALSE(bvh.IsTrackedLight(pPtRaw));
}

} // namespace CanvasUnitTest
