//================================================================================================
// WorldBVHTest -- WorldBVH keeps culling nodes where they are now as the scene graph changes
// after Build.
//
// Uses the real CanvasCore factory and debug mesh data from the CanvasGfx12 device.  No GPU
// work is dispatched; the tests only exercise WorldBVH's primitive bookkeeping and queries.
//================================================================================================

#include "pch.h"
#include "WorldBVH.h"

using namespace Canvas;
using namespace Canvas::Math;

namespace CanvasUnitTest
{

namespace
{

// Debug mesh data for a triangle whose vertices are offset by `origin` in local space.
Gem::TGemPtr<XGfxMeshData> MakeTriangle(XGfxDevice* device, const FloatVector4& origin)
{
    FloatVector4 positions[] = {
        FloatVector4(origin.V[0],        origin.V[1],        origin.V[2], 1.0f),
        FloatVector4(origin.V[0] + 1.0f, origin.V[1],        origin.V[2], 1.0f),
        FloatVector4(origin.V[0],        origin.V[1] + 1.0f, origin.V[2], 1.0f),
    };
    FloatVector4 normals[] = {
        FloatVector4(0.0f, 0.0f, 1.0f, 0.0f),
        FloatVector4(0.0f, 0.0f, 1.0f, 0.0f),
        FloatVector4(0.0f, 0.0f, 1.0f, 0.0f),
    };
    Gem::TGemPtr<XGfxMeshData> pMeshData;
    EXPECT_TRUE(Succeeded(device->CreateDebugMeshData(3, positions, normals, &pMeshData, "Triangle")));
    return pMeshData;
}

// A mesh instance using `meshData`, which may be null.
Gem::TGemPtr<XMeshInstance> MakeMesh(XCanvas* canvas, XGfxMeshData* meshData)
{
    Gem::TGemPtr<XMeshInstance> pMesh;
    EXPECT_TRUE(Succeeded(canvas->CreateMeshInstance(&pMesh)));
    if (meshData)
        pMesh->SetMeshData(meshData);
    return pMesh;
}

// A node at `position` under `parent`, with `element` bound to it.
Gem::TGemPtr<XSceneNode> MakeNode(XCanvas* canvas,
                                  XSceneNode* parent,
                                  const FloatVector4& position,
                                  XSceneElement* element)
{
    Gem::TGemPtr<XSceneNode> pNode;
    EXPECT_TRUE(Succeeded(canvas->CreateSceneNode(&pNode)));
    pNode->SetLocalTranslation(position);
    if (element)
        EXPECT_TRUE(Succeeded(pNode->BindElement(element)));
    EXPECT_TRUE(Succeeded(parent->AddChild(pNode)));
    return pNode;
}

// Frustum of a camera at the origin looking down +X (forward = +X, side = +Y, up = +Z), with a
// 60 degree field of view, near 0.5 and far 100.
Frustum FrustumLookingAlongX()
{
    FloatMatrix4x4 view = {};
    view.M[0][2] = 1.0f; // world X -> view forward (+Z)
    view.M[1][0] = 1.0f; // world Y -> view side (+X)
    view.M[2][1] = 1.0f; // world Z -> view up (+Y)
    view.M[3][3] = 1.0f;
    const FloatMatrix4x4 proj = PerspectiveReverseZ<float>(PiF / 3.0f, 1.0f, 0.5f, 100.0f);
    return Frustum::FromViewProjection(view * proj, true);
}

bool IsVisible(const WorldBVH& bvh, XSceneNode* node)
{
    std::unordered_set<XSceneNode*> visible;
    bvh.QueryFrustum(FrustumLookingAlongX(), visible);
    return visible.count(node) > 0;
}

const FloatVector4 kInView(10.0f, 0.0f, 0.0f, 1.0f);
const FloatVector4 kBehind(-10.0f, 0.0f, 0.0f, 1.0f);
const FloatVector4 kOrigin(0.0f, 0.0f, 0.0f, 1.0f);

} // anonymous namespace

// A node that moves after Build, by its own transform or its parent's, is culled where it is now.
TEST(WorldBVHTest, RefitFollowsMovedNodes)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    auto pTriangle = MakeTriangle(pDevice, kOrigin);
    auto pParent = MakeNode(pCanvas, pWorld->GetRootNode(), kOrigin, nullptr);
    auto pNode = MakeNode(pCanvas, pParent, kBehind, MakeMesh(pCanvas, pTriangle));

    SceneChangeTracker tracker;
    WorldBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    ASSERT_TRUE(bvh.IsRenderableNode(pNode));
    EXPECT_FALSE(IsVisible(bvh, pNode));

    pNode->SetLocalTranslation(kInView);
    bvh.Refit();
    tracker.ClearChanges();
    EXPECT_TRUE(IsVisible(bvh, pNode)) << "Node moved in front of the camera";

    pParent->SetLocalTranslation(FloatVector4(-30.0f, 0.0f, 0.0f, 1.0f));
    bvh.Refit();
    tracker.ClearChanges();
    EXPECT_FALSE(IsVisible(bvh, pNode)) << "Parent moved the node behind the camera";
}

// A mesh with no data at Build, on a node that another mesh makes renderable, is culled by its
// own bounds once it gets data.
TEST(WorldBVHTest, RefitPicksUpBoundsThatBecomeNonEmpty)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // The node sits behind the camera; the late mesh's data reaches in front of it.
    auto pNode = MakeNode(pCanvas, pWorld->GetRootNode(), kBehind,
                          MakeMesh(pCanvas, MakeTriangle(pDevice, kOrigin)));
    auto pLateMesh = MakeMesh(pCanvas, nullptr);
    EXPECT_TRUE(Succeeded(pNode->BindElement(pLateMesh)));

    SceneChangeTracker tracker;
    WorldBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    EXPECT_FALSE(IsVisible(bvh, pNode));

    pLateMesh->SetMeshData(MakeTriangle(pDevice, FloatVector4(20.0f, 0.0f, 0.0f, 1.0f)));
    bvh.Refit();
    tracker.ClearChanges();
    EXPECT_TRUE(IsVisible(bvh, pNode));
}

// A mesh given its first data, or left with none, reports a structure change, so the BVH is
// rebuilt as XWorld::SubmitRenderables does and its node becomes renderable, or stops being
// renderable and is no longer culled.  Other mesh data changes only refit.
TEST(WorldBVHTest, MeshGainingOrLosingBoundsRebuilds)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<XSceneNode> pRoot;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pRoot)));
    auto pMesh = MakeMesh(pCanvas, nullptr);
    auto pNode = MakeNode(pCanvas, pRoot, kBehind, pMesh);

    SceneChangeTracker tracker;
    tracker.WatchStructure(pRoot);
    WorldBVH bvh(tracker);
    auto update = [&]()
    {
        if (tracker.HasStructureChanged())
            bvh.Build(pRoot);
        else
            bvh.Refit();
        tracker.ClearChanges();
    };

    bvh.Build(pRoot);
    tracker.ClearChanges();
    EXPECT_FALSE(bvh.IsRenderableNode(pNode));

    pMesh->SetMeshData(MakeTriangle(pDevice, kOrigin));
    EXPECT_TRUE(tracker.HasStructureChanged()) << "Mesh given its first data";
    update();
    EXPECT_TRUE(bvh.IsRenderableNode(pNode));
    EXPECT_FALSE(IsVisible(bvh, pNode));

    pMesh->SetMeshData(MakeTriangle(pDevice, FloatVector4(20.0f, 0.0f, 0.0f, 1.0f)));
    EXPECT_FALSE(tracker.HasStructureChanged()) << "Mesh given other non-empty data";
    update();
    EXPECT_TRUE(IsVisible(bvh, pNode));

    pMesh->SetMeshData(nullptr);
    EXPECT_TRUE(tracker.HasStructureChanged()) << "Mesh left with no data";
    update();
    EXPECT_FALSE(bvh.IsRenderableNode(pNode));
}

// A node removed from the graph after Build, and released by everything else, stays valid for
// Refit: the BVH holds it until the next Build.
TEST(WorldBVHTest, RefitAfterNodeRemoved)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    auto pNode = MakeNode(pCanvas, pWorld->GetRootNode(), kInView,
                          MakeMesh(pCanvas, MakeTriangle(pDevice, kOrigin)));

    SceneChangeTracker tracker;
    WorldBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    EXPECT_TRUE(Succeeded(pWorld->GetRootNode()->RemoveChild(pNode)));
    pNode = nullptr;

    bvh.Refit();
    tracker.ClearChanges();

    bvh.Build(pWorld->GetRootNode());
    EXPECT_EQ(size_t(0), bvh.RenderableNodeCount());
}

// Giving a material displacement inflates the bounds of the meshes using it without reporting
// their nodes.  It advances the device's mesh bounds generation instead, and the refit after
// InvalidateAllBounds culls the meshes by their inflated bounds.
TEST(WorldBVHTest, RefitAfterInvalidateAllBoundsPicksUpDisplacement)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    Gem::TGemPtr<XGfxMaterial> pMaterial;
    EXPECT_TRUE(Succeeded(pDevice->CreateMaterial(&pMaterial)));

    FloatVector4 positions[] = {
        FloatVector4(0.0f, 0.0f, 0.0f, 1.0f),
        FloatVector4(1.0f, 0.0f, 0.0f, 1.0f),
        FloatVector4(0.0f, 1.0f, 0.0f, 1.0f),
    };
    FloatVector4 normals[] = {
        FloatVector4(0.0f, 0.0f, 1.0f, 0.0f),
        FloatVector4(0.0f, 0.0f, 1.0f, 0.0f),
        FloatVector4(0.0f, 0.0f, 1.0f, 0.0f),
    };
    MeshDataGroupDesc group;
    group.VertexCount = 3;
    group.pPositions  = positions;
    group.pNormals    = normals;
    group.pMaterial   = pMaterial;
    MeshDataDesc desc;
    desc.pGroups    = &group;
    desc.GroupCount = 1;
    Gem::TGemPtr<XGfxMeshData> pMeshData;
    EXPECT_TRUE(Succeeded(pDevice->CreateMeshData(desc, &pMeshData)));

    // Just behind the camera: x spans [-2, -1] until displacement grows it past the near plane.
    auto pNode = MakeNode(pCanvas, pWorld->GetRootNode(), FloatVector4(-2.0f, 0.0f, 0.0f, 1.0f),
                          MakeMesh(pCanvas, pMeshData));

    SceneChangeTracker tracker;
    WorldBVH bvh(tracker);
    bvh.Build(pWorld->GetRootNode());
    EXPECT_FALSE(IsVisible(bvh, pNode));

    const uint64_t generation = pDevice->GetMeshBoundsGeneration();
    GfxDisplacementDesc displacement = {};
    displacement.MapScale = 5.0f;
    pMaterial->SetDisplacement(&displacement);
    EXPECT_NE(generation, pDevice->GetMeshBoundsGeneration());

    bvh.Refit();
    tracker.ClearChanges();
    EXPECT_FALSE(IsVisible(bvh, pNode)) << "No node reported the change";

    bvh.InvalidateAllBounds();
    bvh.Refit();
    tracker.ClearChanges();
    EXPECT_TRUE(IsVisible(bvh, pNode));
}

} // namespace CanvasUnitTest
