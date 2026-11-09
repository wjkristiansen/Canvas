//================================================================================================
// CanvasInterfacesTest
//================================================================================================

#include "pch.h"
using namespace Canvas;

namespace CanvasUnitTest
{
namespace {

// Helpers for equality checks

static bool AlmostZero(float n)
{
    return std::abs(n) < FLT_EPSILON;
}

static bool AlmostEqual(const Canvas::Math::FloatVector4 &a, const Canvas::Math::FloatVector4 &b)
{
    auto d = b - a;
    float lensq = Canvas::Math::DotProduct(d, d);
    return AlmostZero(lensq);
}

static bool AlmostEqual(const Canvas::Math::FloatQuaternion &a, const Canvas::Math::FloatQuaternion &b)
{
    auto d = b - a;
    float lensq = Canvas::Math::DotProduct(d, d);
    return AlmostZero(lensq);
}

static bool AlmostEqual(const Canvas::Math::FloatMatrix4x4 &m0, const Canvas::Math::FloatMatrix4x4 &m1)
{
    for (unsigned int row = 0; row < Canvas::Math::FloatMatrix4x4::Rows; ++row)
    {
        auto d = m1[row] - m0[row];
        float lensq = Canvas::Math::DotProduct(d, d);
        if (!AlmostZero(lensq)) return false;
    }
    return true;
}

} // anonymous namespace

TEST(CanvasInterfacesTest, SimpleInterfaces)
{
    // Create XCanvas object
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    // Create XWorld object
    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // Create an empty XSceneGraphNode
    Gem::TGemPtr<Canvas::XSceneGraphNode> pSceneGraphNode;
    const char szNodeName[] = "NullNode";
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pSceneGraphNode)));
    pSceneGraphNode->SetName(szNodeName);

    // Verify QI for XGeneric
    Gem::TGemPtr<XGeneric> pGeneric;
    EXPECT_TRUE(Succeeded(pSceneGraphNode->QueryInterface(&pGeneric)));

    // Validate the name
    EXPECT_TRUE(0 == strncmp(pSceneGraphNode->GetName(), szNodeName, _countof(szNodeName)));
}

TEST(CanvasInterfacesTest, WorldUIGraphs)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "UIWorld")));

    EXPECT_EQ(Gem::Result::BadPointer, pWorld->CreateUIGraph(nullptr));

    // The world creates its UI graphs on its own device
    Gem::TGemPtr<Canvas::XUIGraph> pHud;
    Gem::TGemPtr<Canvas::XUIGraph> pMenu;
    ASSERT_TRUE(Succeeded(pWorld->CreateUIGraph(&pHud)));
    ASSERT_TRUE(Succeeded(pWorld->CreateUIGraph(&pMenu)));
    ASSERT_NE(nullptr, pHud.Get());
    ASSERT_NE(nullptr, pMenu.Get());
    EXPECT_NE(pHud.Get(), pMenu.Get());
    EXPECT_EQ(pDevice.Get(), pHud->GetDevice());

    Gem::TGemPtr<Canvas::XUIGraphNode> pNode;
    ASSERT_TRUE(Succeeded(pHud->CreateNode(nullptr, &pNode)));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    // A UI graph created on its own is not one of the world's
    Gem::TGemPtr<Canvas::XUIGraph> pConsole;
    ASSERT_TRUE(Succeeded(pCanvas->CreateUIGraph(pDevice, &pConsole)));
    EXPECT_EQ(Gem::Result::NotFound, pWorld->RemoveUIGraph(pConsole));
    EXPECT_EQ(Gem::Result::NotFound, pWorld->RemoveUIGraph(nullptr));

    EXPECT_TRUE(Succeeded(pWorld->RemoveUIGraph(pMenu)));
    EXPECT_EQ(Gem::Result::NotFound, pWorld->RemoveUIGraph(pMenu));
    EXPECT_TRUE(Succeeded(pWorld->RemoveUIGraph(pHud)));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));
}

TEST(CanvasInterfacesTest, WorldObjectNodes)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "ObjectWorld")));
    Canvas::XSceneGraphNode *pRoot = pWorld->GetRootNode();

    // A windmill with an arrow in one of its blades, beside hand-built ground
    Gem::TGemPtr<Canvas::XSceneGraphNode> pWindmillNode, pShaft, pBlade1, pBlade2, pGround, pArrowNode, pTip;
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pWindmillNode, "Windmill")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pShaft, "Shaft")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pBlade1, "Blade1")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pBlade2, "Blade2")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pGround, "Ground")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pArrowNode, "Arrow")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pTip, "Tip")));
    ASSERT_TRUE(Succeeded(pRoot->AddChild(pWindmillNode)));
    ASSERT_TRUE(Succeeded(pWindmillNode->AddChild(pShaft)));
    ASSERT_TRUE(Succeeded(pShaft->AddChild(pBlade1)));
    ASSERT_TRUE(Succeeded(pShaft->AddChild(pBlade2)));
    ASSERT_TRUE(Succeeded(pRoot->AddChild(pGround)));
    ASSERT_TRUE(Succeeded(pBlade2->AddChild(pArrowNode)));
    ASSERT_TRUE(Succeeded(pArrowNode->AddChild(pTip)));

    // Before any world object exists, no node belongs to one
    EXPECT_EQ(nullptr, pBlade2->GetWorldObject());

    Gem::TGemPtr<Canvas::XWorldObject> pWindmill, pArrow;
    ASSERT_TRUE(Succeeded(pWorld->CreateWorldObject(pWindmillNode, &pWindmill, "Windmill")));
    ASSERT_TRUE(Succeeded(pWorld->CreateWorldObject(pArrowNode, &pArrow)));  // unnamed
    EXPECT_EQ(pWindmillNode.Get(), pWindmill->GetAnchorNode());
    EXPECT_EQ(pWorld.Get(), pWindmill->GetWorld());
    EXPECT_STREQ("Windmill", pWindmill->GetName());

    // An anchor node belongs to its world object; other nodes inherit their parent's
    EXPECT_EQ(nullptr, pRoot->GetWorldObject());
    EXPECT_EQ(nullptr, pGround->GetWorldObject());
    EXPECT_EQ(pWindmill.Get(), pWindmillNode->GetWorldObject());
    EXPECT_EQ(pWindmill.Get(), pShaft->GetWorldObject());
    EXPECT_EQ(pWindmill.Get(), pBlade1->GetWorldObject());
    EXPECT_EQ(pWindmill.Get(), pBlade2->GetWorldObject());
    EXPECT_EQ(pArrow.Get(), pArrowNode->GetWorldObject());
    EXPECT_EQ(pArrow.Get(), pTip->GetWorldObject());

    // Inheritance follows the graph: moving the shaft under the ground moves its nodes out
    // of the windmill, and the arrow's nodes stay with the arrow
    ASSERT_TRUE(Succeeded(pGround->AddChild(pShaft)));
    EXPECT_EQ(nullptr, pBlade2->GetWorldObject());
    EXPECT_EQ(pArrow.Get(), pTip->GetWorldObject());
}

TEST(CanvasInterfacesTest, WorldObjectDetachedAndUnanchored)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "DetachedWorld")));

    // A world object with no anchor node has no nodes
    Gem::TGemPtr<Canvas::XWorldObject> pRules;
    ASSERT_TRUE(Succeeded(pWorld->CreateWorldObject(nullptr, &pRules, "Rules")));
    EXPECT_EQ(nullptr, pRules->GetAnchorNode());
    EXPECT_EQ(pWorld.Get(), pRules->GetWorld());

    // A world object anchored at a node in no scene graph is detached; adding its anchor
    // node to the graph puts it in the scene graph
    Gem::TGemPtr<Canvas::XSceneGraphNode> pProjectileNode, pTrail;
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pProjectileNode, "Projectile")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pTrail, "Trail")));
    ASSERT_TRUE(Succeeded(pProjectileNode->AddChild(pTrail)));
    Gem::TGemPtr<Canvas::XWorldObject> pProjectile;
    ASSERT_TRUE(Succeeded(pWorld->CreateWorldObject(pProjectileNode, &pProjectile, "Projectile")));
    EXPECT_EQ(nullptr, pProjectileNode->GetParent());
    EXPECT_EQ(pProjectile.Get(), pTrail->GetWorldObject());
    EXPECT_TRUE(Succeeded(pWorld->Update(0.016f)));

    ASSERT_TRUE(Succeeded(pWorld->GetRootNode()->AddChild(pProjectileNode)));
    EXPECT_EQ(pProjectile.Get(), pTrail->GetWorldObject());
    EXPECT_TRUE(Succeeded(pWorld->Update(0.016f)));
}

TEST(CanvasInterfacesTest, WorldObjectCreationErrors)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<Canvas::XWorld> pWorld, pOtherWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "World")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pOtherWorld, "OtherWorld")));

    EXPECT_EQ(Gem::Result::BadPointer, pWorld->CreateWorldObject(nullptr, nullptr));

    Gem::TGemPtr<Canvas::XWorldObject> pObject;

    // The world's root node is not an anchor node
    EXPECT_EQ(Gem::Result::InvalidArg, pWorld->CreateWorldObject(pWorld->GetRootNode(), &pObject));
    EXPECT_EQ(nullptr, pObject.Get());
    EXPECT_EQ(nullptr, pWorld->GetRootNode()->GetWorldObject());

    // A node anchors one world object at most
    Gem::TGemPtr<Canvas::XSceneGraphNode> pNode;
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pNode, "Node")));
    ASSERT_TRUE(Succeeded(pWorld->CreateWorldObject(pNode, &pObject)));
    Gem::TGemPtr<Canvas::XWorldObject> pSecond;
    EXPECT_EQ(Gem::Result::InvalidArg, pWorld->CreateWorldObject(pNode, &pSecond));
    EXPECT_EQ(Gem::Result::InvalidArg, pOtherWorld->CreateWorldObject(pNode, &pSecond));
    EXPECT_EQ(nullptr, pSecond.Get());
    EXPECT_EQ(pObject.Get(), pNode->GetWorldObject());
}

TEST(CanvasInterfacesTest, WorldObjectLifetime)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<Canvas::XWorldObject> pObject;
    Gem::TGemPtr<Canvas::XSceneGraphNode> pAnchor, pChild;
    {
        Gem::TGemPtr<Canvas::XWorld> pWorld;
        ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "ShortWorld")));
        ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pAnchor, "Survivor")));
        ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pChild, "Child")));
        ASSERT_TRUE(Succeeded(pAnchor->AddChild(pChild)));
        ASSERT_TRUE(Succeeded(pWorld->GetRootNode()->AddChild(pAnchor)));
        ASSERT_TRUE(Succeeded(pWorld->CreateWorldObject(pAnchor, &pObject, "Survivor")));
    }

    // A world object the app holds outlives its world, with its anchor node and the
    // nodes below it; the anchor node no longer has a parent
    EXPECT_EQ(nullptr, pObject->GetWorld());
    EXPECT_EQ(pAnchor.Get(), pObject->GetAnchorNode());
    EXPECT_EQ(nullptr, pAnchor->GetParent());
    EXPECT_EQ(pObject.Get(), pAnchor->GetWorldObject());
    EXPECT_EQ(pObject.Get(), pChild->GetWorldObject());
    (void)pChild->GetGlobalMatrix();

    // Releasing the world object clears its anchor node's back-reference
    pObject = nullptr;
    EXPECT_EQ(nullptr, pAnchor->GetWorldObject());
    EXPECT_EQ(nullptr, pChild->GetWorldObject());
}

TEST(CanvasInterfacesTest, SceneGraphNodesTest)
{
    // Create XCanvas object
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    // Create XWorld object
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // Create nodes
    Gem::TGemPtr<XSceneGraphNode> pNodes[6];
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[0])));
    pNodes[0]->SetName("Node0");
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[1])));
    pNodes[1]->SetName("Node1");
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[2])));
    pNodes[2]->SetName("Node2");
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[3])));
    pNodes[3]->SetName("Node3");
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[4])));
    pNodes[4]->SetName("Node4");
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[5])));
    pNodes[5]->SetName("Node5");

    // Build the following tree
    // Root
    //  - Node0
    //      - Node2
    //      - Node3
    //  - Node1
    //      - Node4
    //          - Node5

    Gem::TGemPtr<XSceneGraphNode> pRoot = pWorld->GetRootNode();
    EXPECT_NE(pRoot.Get(), nullptr);
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pNodes[0])));
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pNodes[1])));
    EXPECT_TRUE(Succeeded(pNodes[0]->AddChild(pNodes[2])));
    EXPECT_TRUE(Succeeded(pNodes[0]->AddChild(pNodes[3])));
    EXPECT_TRUE(Succeeded(pNodes[1]->AddChild(pNodes[4])));
    EXPECT_TRUE(Succeeded(pNodes[4]->AddChild(pNodes[5])));
}

TEST(CanvasInterfacesTest, SceneGraphTransforms)
{
    using namespace Canvas::Math;

    // Create Canvas and Scene
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // Create a small hierarchy: root -> A -> B
    Gem::TGemPtr<XSceneGraphNode> pA, pB;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pA)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pB)));

    Gem::TGemPtr<XSceneGraphNode> pRoot = pWorld->GetRootNode();
    EXPECT_NE(pRoot.Get(), nullptr);
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pA)));
    EXPECT_TRUE(Succeeded(pA->AddChild(pB)));

    // Set locals
    // A: rotate 90 deg around Z, translate (1,2,0)
    const float ninety = float(3.14159265358979323846 / 2.0);
    auto qA = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, ninety);
    pA->SetLocalRotation(qA);
    pA->SetLocalTranslation(FloatVector4(1.0f, 2.0f, 0.0f, 1.0f));

    // B: rotate 90 deg around X, translate (1,0,0)
    auto qB = FloatQuaternion::FromEulerXYZ(ninety, 0.0f, 0.0f);
    pB->SetLocalRotation(qB);
    pB->SetLocalTranslation(FloatVector4(1.0f, 0.0f, 0.0f, 1.0f));

    // Verify A local matrix matches quaternion + translation
    auto mA_local = pA->GetLocalMatrix();
    auto mA_expected = QuaternionToRotationMatrix(qA);
    mA_expected[3][0] = 1.0f; mA_expected[3][1] = 2.0f; mA_expected[3][2] = 0.0f; mA_expected[3][3] = 1.0f;
    EXPECT_TRUE(AlmostEqual(mA_local, mA_expected));

    // Verify B local matrix
    auto mB_local = pB->GetLocalMatrix();
    auto mB_expected = QuaternionToRotationMatrix(qB);
    mB_expected[3][0] = 1.0f; mB_expected[3][1] = 0.0f; mB_expected[3][2] = 0.0f; mB_expected[3][3] = 1.0f;
    EXPECT_TRUE(AlmostEqual(mB_local, mB_expected));

    // Global rotations: Rg(A) = qA, Rg(B) = qA * qB
    auto qA_g = pA->GetGlobalRotation();
    auto qB_g = pB->GetGlobalRotation();
    EXPECT_TRUE(AlmostEqual(qA_g, qA.Normalize()));
    EXPECT_TRUE(AlmostEqual(qB_g, (qA * qB).Normalize()));

    // Global translations:
    // Tg(A) = rotate(qRoot, (1,2,0)) + Troot = (1,2,0)
    // Tg(B) = Tg(A) + rotate(Rg(A), (1,0,0)) = (1,2,0) + (0,1,0) = (1,3,0)
    auto tA_g = pA->GetGlobalTranslation();
    auto tB_g = pB->GetGlobalTranslation();
    EXPECT_TRUE(AlmostEqual(tA_g, FloatVector4(1.0f, 2.0f, 0.0f, 1.0f)));
    EXPECT_TRUE(AlmostEqual(tB_g, FloatVector4(1.0f, 3.0f, 0.0f, 1.0f)));

    // Global matrices: Mg = Mlocal * Mparent (row vectors), consistent with
    // GetGlobalRotation (qA * qB) and GetGlobalTranslation above.
    auto mA_global = pA->GetGlobalMatrix();
    auto mB_global = pB->GetGlobalMatrix();
    auto mRoot = FloatMatrix4x4::Identity();
    auto mA_global_expected = mA_local * mRoot; // root is identity
    auto mB_global_expected = mB_local * mA_global_expected;
    EXPECT_TRUE(AlmostEqual(mA_global, mA_global_expected));
    EXPECT_TRUE(AlmostEqual(mB_global, mB_global_expected));

    // The matrix path and the quaternion path must describe the same global transform:
    // row 3 of the global matrix is the global translation, and its rotation block is
    // the global rotation's matrix.
    EXPECT_TRUE(AlmostEqual(mB_global[3], tB_g));
    auto mB_rotation = mB_global;
    mB_rotation[3] = FloatVector4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(AlmostEqual(mB_rotation, QuaternionToRotationMatrix(qB_g)));

    // Test scenario: Move B from A to Root after transforms are resolved
    // This tests that dirty flags are properly set when parent changes

    // First, create a new node C under Root for verification
    Gem::TGemPtr<XSceneGraphNode> pC;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pC)));
    auto qC = FloatQuaternion::FromEulerXYZ(0.0f, ninety, 0.0f);  // 90° around Y
    pC->SetLocalRotation(qC);
    pC->SetLocalTranslation(FloatVector4(0.0f, 0.0f, 1.0f, 1.0f));
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pC)));

    // Verify C's initial global transforms
    auto qC_g_initial = pC->GetGlobalRotation();
    auto tC_g_initial = pC->GetGlobalTranslation();
    EXPECT_TRUE(AlmostEqual(qC_g_initial, qC.Normalize()));
    EXPECT_TRUE(AlmostEqual(tC_g_initial, FloatVector4(0.0f, 0.0f, 1.0f, 1.0f)));

    // Now move B from A to C (B was previously under A)
    // B's local is: rotation 90° around X, translation (1,0,0)
    // When B is under C (which has 90° Y rotation and translation (0,0,1)):
    //   - B's global rotation should be: qC * qB
    //   - B's global translation should be: tC + rotate(qC, (1,0,0)) = (0,0,1) + (0,0,-1) = (0,0,0)

    EXPECT_TRUE(Succeeded(pC->AddChild(pB)));

    // After moving, B's global transforms should reflect new parent C
    auto qB_g_moved = pB->GetGlobalRotation();
    auto tB_g_moved = pB->GetGlobalTranslation();
    auto mB_global_moved = pB->GetGlobalMatrix();

    // Expected: qC * qB
    auto qB_g_expected_moved = (qC * qB).Normalize();
    EXPECT_TRUE(AlmostEqual(qB_g_moved, qB_g_expected_moved));

    // Expected translation: C's translation (0,0,1) + rotate(qC, B's local translation (1,0,0))
    // qC rotates (1,0,0) by 90° around Y: (1,0,0) -> (0,0,-1)
    const FloatQuaternion vB_local(1.0f, 0.0f, 0.0f, 0.0f);
    const auto qC_conj = Conjugate(qC.Normalize());
    const auto vB_rotated = qC.Normalize() * vB_local * qC_conj;
    FloatVector4 tB_expected_moved = tC_g_initial + FloatVector4(vB_rotated.X, vB_rotated.Y, vB_rotated.Z, 0.0f);
    tB_expected_moved.W = 1.0f;
    EXPECT_TRUE(AlmostEqual(tB_g_moved, tB_expected_moved));

    // Expected matrix: B_local * C_global (local-first, row-vector convention)
    auto mC_global = pC->GetGlobalMatrix();
    auto mB_global_expected_moved = mB_local * mC_global;
    EXPECT_TRUE(AlmostEqual(mB_global_moved, mB_global_expected_moved));

    // Verify that A's child is no longer B (B was moved to C)
    auto pA_firstChild = pA->GetFirstChild();
    EXPECT_TRUE(pA_firstChild == nullptr || pA_firstChild != pB.Get());

    // Test that changing a parent's transform propagates to descendants
    // B is currently a child of C. If we change C's rotation/translation,
    // B's global transforms should update accordingly.

    // Get B's current global matrix and local matrix (local shouldn't change)
    auto mB_before_parent_change = pB->GetGlobalMatrix();
    auto mB_local_saved = pB->GetLocalMatrix();

    // Change C's local translation
    pC->SetLocalTranslation(FloatVector4(5.0f, 5.0f, 5.0f, 1.0f));

    // B's local matrix should remain unchanged
    auto mB_local_check = pB->GetLocalMatrix();
    EXPECT_TRUE(AlmostEqual(mB_local_saved, mB_local_check));

    // B's global matrix should now be different (dirty flags propagated)
    auto mB_after_translation = pB->GetGlobalMatrix();
    EXPECT_FALSE(AlmostEqual(mB_before_parent_change, mB_after_translation));

    // Verify the new value is correct: B_local * C_new_global (local-first)
    auto mC_global_new = pC->GetGlobalMatrix();
    auto mB_expected_new = mB_local_saved * mC_global_new;
    EXPECT_TRUE(AlmostEqual(mB_after_translation, mB_expected_new));

    // Change C's rotation
    auto qC_new = FloatQuaternion::FromEulerXYZ(ninety, 0.0f, 0.0f);  // 90° around X instead of Y
    pC->SetLocalRotation(qC_new);

    // B's global transforms should update again
    auto mB_after_rotation = pB->GetGlobalMatrix();
    EXPECT_FALSE(AlmostEqual(mB_after_translation, mB_after_rotation));

    // Verify correctness after rotation change
    auto mC_global_final = pC->GetGlobalMatrix();
    auto mB_expected_final = mB_local_saved * mC_global_final;
    EXPECT_TRUE(AlmostEqual(mB_after_rotation, mB_expected_final));
}

TEST(CanvasInterfacesTest, SceneGraphGlobalTranslationInheritsScale)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    Gem::TGemPtr<XSceneGraphNode> pParent, pChild;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pParent)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pChild)));
    EXPECT_TRUE(Succeeded(pWorld->GetRootNode()->AddChild(pParent)));
    EXPECT_TRUE(Succeeded(pParent->AddChild(pChild)));

    // Parent: non-uniform scale (2,3,1), then +90 deg about Z, then translate (10,0,0).
    const float ninety = float(3.14159265358979323846 / 2.0);
    pParent->SetLocalScale(FloatVector4(2.0f, 3.0f, 1.0f, 0.0f));
    pParent->SetLocalRotation(FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, ninety));
    pParent->SetLocalTranslation(FloatVector4(10.0f, 0.0f, 0.0f, 1.0f));
    pChild->SetLocalTranslation(FloatVector4(1.0f, 1.0f, 0.0f, 1.0f));

    // (1,1,0) scaled -> (2,3,0), rotated -> (-3,2,0), translated -> (7,2,0).
    const FloatVector4 expected(7.0f, 2.0f, 0.0f, 1.0f);
    EXPECT_TRUE(AlmostEqual(pChild->GetGlobalTranslation(), expected));
    EXPECT_TRUE(AlmostEqual(pChild->GetGlobalMatrix()[3], expected));

    // Changing the parent's scale must refresh the child's global translation.
    // (1,1,0) rotated -> (-1,1,0), translated -> (9,1,0).
    pParent->SetLocalScale(FloatVector4(1.0f, 1.0f, 1.0f, 0.0f));
    EXPECT_TRUE(AlmostEqual(pChild->GetGlobalTranslation(), FloatVector4(9.0f, 1.0f, 0.0f, 1.0f)));
}

TEST(CanvasInterfacesTest, SceneGraphLocalTransform)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    Gem::TGemPtr<XSceneGraphNode> pParent, pChild;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pParent)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pChild)));
    EXPECT_TRUE(Succeeded(pWorld->GetRootNode()->AddChild(pParent)));
    EXPECT_TRUE(Succeeded(pParent->AddChild(pChild)));
    pChild->SetLocalTranslation(FloatVector4(1.0f, 1.0f, 0.0f, 1.0f));

    // A new node's local transform is identity.
    const Transform identity;
    EXPECT_TRUE(AlmostEqual(pParent->GetLocalTransform().Scale, identity.Scale));
    EXPECT_TRUE(AlmostEqual(pParent->GetLocalTransform().Rotation, identity.Rotation));
    EXPECT_TRUE(AlmostEqual(pParent->GetLocalTransform().Translation, identity.Translation));
    EXPECT_TRUE(AlmostEqual(pChild->GetGlobalTranslation(), FloatVector4(1.0f, 1.0f, 0.0f, 1.0f)));

    // Setting the whole transform sets each component and refreshes descendants.
    const float ninety = float(3.14159265358979323846 / 2.0);
    Transform t;
    t.Scale       = FloatVector4(2.0f, 3.0f, 1.0f, 0.0f);
    t.Rotation    = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, ninety);
    t.Translation = FloatVector4(10.0f, 0.0f, 0.0f, 1.0f);
    pParent->SetLocalTransform(t);

    EXPECT_TRUE(AlmostEqual(pParent->GetLocalScale(), t.Scale));
    EXPECT_TRUE(AlmostEqual(pParent->GetLocalRotation(), t.Rotation));
    EXPECT_TRUE(AlmostEqual(pParent->GetLocalTranslation(), t.Translation));

    // Scale rows of the +90 deg Z rotation, translation in row 3.
    const FloatMatrix4x4 expectedLocal({
        {  0.0f, 2.0f, 0.0f, 0.0f },
        { -3.0f, 0.0f, 0.0f, 0.0f },
        {  0.0f, 0.0f, 1.0f, 0.0f },
        { 10.0f, 0.0f, 0.0f, 1.0f },
    });
    EXPECT_TRUE(AlmostEqual(pParent->GetLocalMatrix(), expectedLocal));
    EXPECT_TRUE(AlmostEqual(t.ToMatrix(), expectedLocal));

    // (1,1,0) scaled -> (2,3,0), rotated -> (-3,2,0), translated -> (7,2,0).
    EXPECT_TRUE(AlmostEqual(pChild->GetGlobalTranslation(), FloatVector4(7.0f, 2.0f, 0.0f, 1.0f)));
    EXPECT_TRUE(AlmostEqual(pChild->GetGlobalRotation(), t.Rotation));
}

TEST(CanvasInterfacesTest, CameraTest)
{
    using namespace Canvas::Math;

    // Create Canvas and Scene
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // Create a hierarchy: root -> A -> B -> cameraNode
    Gem::TGemPtr<XSceneGraphNode> pA, pB, pCameraNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pA)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pB)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pCameraNode)));

    Gem::TGemPtr<XSceneGraphNode> pRoot = pWorld->GetRootNode();
    EXPECT_NE(pRoot.Get(), nullptr);
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pA)));
    EXPECT_TRUE(Succeeded(pA->AddChild(pB)));
    EXPECT_TRUE(Succeeded(pB->AddChild(pCameraNode)));

    // Create camera and attach to cameraNode
    Gem::TGemPtr<XCamera> pCamera;
    EXPECT_TRUE(Succeeded(pCanvas->CreateCamera(&pCamera)));
    EXPECT_TRUE(Succeeded(pCameraNode->BindElement(pCamera)));

    // Set initial camera parameters
    const float fov = float(3.14159265358979323846 / 4.0); // 45 degrees
    const float aspect = 16.0f / 9.0f;
    const float nearClip = 0.1f;
    const float farClip = 1000.0f;
    pCamera->SetFovAngle(fov);
    pCamera->SetAspectRatio(aspect);
    pCamera->SetNearClip(nearClip);
    pCamera->SetFarClip(farClip);

    // Set transforms for the hierarchy
    const float ninety = float(3.14159265358979323846 / 2.0);
    auto qA = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, ninety);
    pA->SetLocalRotation(qA);
    pA->SetLocalTranslation(FloatVector4(1.0f, 0.0f, 0.0f, 1.0f));

    auto qB = FloatQuaternion::FromEulerXYZ(ninety, 0.0f, 0.0f);
    pB->SetLocalRotation(qB);
    pB->SetLocalTranslation(FloatVector4(0.0f, 1.0f, 0.0f, 1.0f));

    pCameraNode->SetLocalTranslation(FloatVector4(0.0f, 0.0f, 5.0f, 1.0f));

    // Update scene graph and camera (simulates frame update)
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    // Get initial matrices - this will compute and cache them
    auto viewMatrix1 = pCamera->GetViewMatrix();
    auto projMatrix1 = pCamera->GetProjectionMatrix();
    auto viewProjMatrix1 = pCamera->GetViewProjectionMatrix();

    // Verify matrices are not identity
    auto identity = FloatMatrix4x4::Identity();
    EXPECT_FALSE(AlmostEqual(viewMatrix1, identity)) << "View matrix should not be identity";
    EXPECT_FALSE(AlmostEqual(projMatrix1, identity)) << "Projection matrix should not be identity";

    // Projection now produces standard D3D LHS clip space from a
    // view space where +X=right, +Y=up, +Z=forward. The matrix
    // should look like:
    // Row 0: [ f/aspect, 0,    0,    0 ]  (x_view=right    -> x_clip)
    // Row 1: [    0,     f,    0,    0 ]  (y_view=up       -> y_clip)
    // Row 2: [    0,     0,    A,    1 ]  (z_view=forward  -> z_clip + w_clip)
    // Row 3: [    0,     0,    B,    0 ]  (const           -> z_clip depth bias)

    // Check specific elements to validate the projection matrix structure
    const float tolerance = 1e-5f;
    float f = 1.0f / std::tan(fov * 0.5f);  // approx 2.414

    // Row 0, col 0: f / aspect (right -> x_clip)
    EXPECT_TRUE(std::abs(projMatrix1[0][0] - f / aspect) < 0.01f) << "proj[0][0] should be approximately f/aspect";

    // Row 1, col 1: f (up -> y_clip)
    EXPECT_TRUE(std::abs(projMatrix1[1][1] - f) < 0.01f) << "proj[1][1] should be approximately 2.414 (f)";

    // Row 2, col 3: Should have 1.0 for perspective divide (forward -> w_clip)
    EXPECT_TRUE(std::abs(projMatrix1[2][3] - 1.0f) < tolerance) << "proj[2][3] should be 1.0 for perspective divide";

    // The old shape's hot cells should now be zero.
    EXPECT_TRUE(std::abs(projMatrix1[0][3]) < tolerance) << "proj[0][3] should be 0 in new convention";
    EXPECT_TRUE(std::abs(projMatrix1[1][0]) < tolerance) << "proj[1][0] should be 0 in new convention";
    EXPECT_TRUE(std::abs(projMatrix1[2][1]) < tolerance) << "proj[2][1] should be 0 in new convention";
    EXPECT_TRUE(std::abs(projMatrix1[3][3]) < tolerance) << "proj[3][3] should be 0";

    // Verify view-projection is the product of view and projection
    auto expectedViewProj = viewMatrix1 * projMatrix1;
    EXPECT_TRUE(AlmostEqual(viewProjMatrix1, expectedViewProj));

    // Test 1: Modify camera node's local transform
    // This should invalidate view matrix after Update()
    pCameraNode->SetLocalTranslation(FloatVector4(0.0f, 0.0f, 10.0f, 1.0f));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    auto viewMatrix2 = pCamera->GetViewMatrix();
    EXPECT_FALSE(AlmostEqual(viewMatrix1, viewMatrix2)); // Should be different

    // Projection should remain the same (no parameters changed)
    auto projMatrix2 = pCamera->GetProjectionMatrix();
    EXPECT_TRUE(AlmostEqual(projMatrix1, projMatrix2));

    // View-projection should be different
    auto viewProjMatrix2 = pCamera->GetViewProjectionMatrix();
    EXPECT_FALSE(AlmostEqual(viewProjMatrix1, viewProjMatrix2));

    // Test 2: Modify parent node B's transform
    // This should invalidate camera's view matrix (ancestor changed)
    pB->SetLocalTranslation(FloatVector4(0.0f, 2.0f, 0.0f, 1.0f));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    auto viewMatrix3 = pCamera->GetViewMatrix();
    EXPECT_FALSE(AlmostEqual(viewMatrix2, viewMatrix3)); // Should be different from previous

    // Test 3: Modify grandparent node A's transform
    // This should also invalidate camera's view matrix
    pA->SetLocalTranslation(FloatVector4(2.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    auto viewMatrix4 = pCamera->GetViewMatrix();
    EXPECT_FALSE(AlmostEqual(viewMatrix3, viewMatrix4)); // Should be different

    // Test 4: Modify projection parameters
    // This should invalidate projection and view-projection matrices
    pCamera->SetFovAngle(float(3.14159265358979323846 / 3.0)); // 60 degrees
    auto projMatrix3 = pCamera->GetProjectionMatrix();
    EXPECT_FALSE(AlmostEqual(projMatrix2, projMatrix3)); // Should be different

    auto viewProjMatrix3 = pCamera->GetViewProjectionMatrix();
    EXPECT_FALSE(AlmostEqual(viewProjMatrix2, viewProjMatrix3));

    // Verify new view-projection is correct product
    auto viewMatrixCurrent = pCamera->GetViewMatrix();
    auto projMatrixCurrent = pCamera->GetProjectionMatrix();
    auto expectedViewProjCurrent = viewMatrixCurrent * projMatrixCurrent;
    EXPECT_TRUE(AlmostEqual(viewProjMatrix3, expectedViewProjCurrent));

    // Test 5: Modify aspect ratio
    pCamera->SetAspectRatio(4.0f / 3.0f);
    auto projMatrix4 = pCamera->GetProjectionMatrix();
    EXPECT_FALSE(AlmostEqual(projMatrix3, projMatrix4));

    // Test 6: Modify near/far clip planes
    pCamera->SetNearClip(0.5f);
    auto projMatrix5 = pCamera->GetProjectionMatrix();
    EXPECT_FALSE(AlmostEqual(projMatrix4, projMatrix5));

    pCamera->SetFarClip(500.0f);
    auto projMatrix6 = pCamera->GetProjectionMatrix();
    EXPECT_FALSE(AlmostEqual(projMatrix5, projMatrix6));
}

TEST(CanvasInterfacesTest, CameraNodeReparenting)
{
    using namespace Canvas::Math;

    // Create Canvas and Scene
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // Create two separate branches: root -> A and root -> B
    Gem::TGemPtr<XSceneGraphNode> pA, pB, pCameraNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pA)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pB)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pCameraNode)));

    Gem::TGemPtr<XSceneGraphNode> pRoot = pWorld->GetRootNode();
    EXPECT_NE(pRoot.Get(), nullptr);

    // Initial setup: cameraNode is under A
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pA)));
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pB)));
    EXPECT_TRUE(Succeeded(pA->AddChild(pCameraNode)));

    // Create camera and attach
    Gem::TGemPtr<XCamera> pCamera;
    EXPECT_TRUE(Succeeded(pCanvas->CreateCamera(&pCamera)));
    EXPECT_TRUE(Succeeded(pCameraNode->BindElement(pCamera)));

    // Set different transforms for A and B
    const float ninety = float(3.14159265358979323846 / 2.0);
    auto qA = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, ninety); // Rotate 90° around Z
    pA->SetLocalRotation(qA);
    pA->SetLocalTranslation(FloatVector4(5.0f, 0.0f, 0.0f, 1.0f));

    auto qB = FloatQuaternion::FromEulerXYZ(0.0f, ninety, 0.0f); // Rotate 90° around Y
    pB->SetLocalRotation(qB);
    pB->SetLocalTranslation(FloatVector4(0.0f, 5.0f, 0.0f, 1.0f));

    pCameraNode->SetLocalTranslation(FloatVector4(1.0f, 0.0f, 0.0f, 1.0f));

    // Update and get initial view matrix (camera is under A)
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));
    auto viewMatrix1 = pCamera->GetViewMatrix();
    auto viewProjMatrix1 = pCamera->GetViewProjectionMatrix();

    // Move camera node from A to B
    // This changes the camera's parent, so its world transform will change
    EXPECT_TRUE(Succeeded(pB->AddChild(pCameraNode)));

    // Update scene to mark view matrix dirty
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    // Get new matrices - should be different due to different parent transform
    auto viewMatrix2 = pCamera->GetViewMatrix();
    auto viewProjMatrix2 = pCamera->GetViewProjectionMatrix();

    // Matrices should be different (different parent transforms)
    EXPECT_FALSE(AlmostEqual(viewMatrix1, viewMatrix2));
    EXPECT_FALSE(AlmostEqual(viewProjMatrix1, viewProjMatrix2));

    // The view matrix bakes in the world(X-fwd, Y-left, Z-up) ->
    // view(X-right, Y-up, Z-fwd) basis remap, so world * view is no
    // longer identity.  The invariant that does hold is that the
    // camera's world-space position maps to the view-space origin.
    auto cameraWorldMatrix = pCameraNode->GetGlobalMatrix();
    FloatVector4 cameraWorldPos(
        cameraWorldMatrix[3][0], cameraWorldMatrix[3][1],
        cameraWorldMatrix[3][2], 1.0f);
    FloatVector4 cameraViewPos = cameraWorldPos * viewMatrix2;
    EXPECT_TRUE(std::abs(cameraViewPos.X) < 1e-4f);
    EXPECT_TRUE(std::abs(cameraViewPos.Y) < 1e-4f);
    EXPECT_TRUE(std::abs(cameraViewPos.Z) < 1e-4f);
    EXPECT_TRUE(std::abs(cameraViewPos.W - 1.0f) < 1e-4f);

    // Test moving to root (no parent transform)
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pCameraNode)));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    auto viewMatrix3 = pCamera->GetViewMatrix();
    EXPECT_FALSE(AlmostEqual(viewMatrix2, viewMatrix3));

    // With only local transform, world matrix should equal local matrix
    auto cameraLocalMatrix = pCameraNode->GetLocalMatrix();
    auto cameraWorldMatrix2 = pCameraNode->GetGlobalMatrix();
    EXPECT_TRUE(AlmostEqual(cameraLocalMatrix, cameraWorldMatrix2));
}

TEST(CanvasInterfacesTest, CameraMatrixConsistency)
{
    using namespace Canvas::Math;

    // Create Canvas and Scene
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // Create camera node
    Gem::TGemPtr<XSceneGraphNode> pCameraNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pCameraNode)));

    Gem::TGemPtr<XSceneGraphNode> pRoot = pWorld->GetRootNode();
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pCameraNode)));

    // Create and attach camera
    Gem::TGemPtr<XCamera> pCamera;
    EXPECT_TRUE(Succeeded(pCanvas->CreateCamera(&pCamera)));
    EXPECT_TRUE(Succeeded(pCameraNode->BindElement(pCamera)));

    // Set camera parameters
    pCamera->SetFovAngle(float(3.14159265358979323846 / 4.0));
    pCamera->SetAspectRatio(16.0f / 9.0f);
    pCamera->SetNearClip(0.1f);
    pCamera->SetFarClip(1000.0f);

    // Position camera
    const float ninety = float(3.14159265358979323846 / 2.0);
    auto qCamera = FloatQuaternion::FromEulerXYZ(0.0f, ninety, 0.0f);
    pCameraNode->SetLocalRotation(qCamera);
    pCameraNode->SetLocalTranslation(FloatVector4(10.0f, 5.0f, 0.0f, 1.0f));

    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    // Test: Calling GetViewProjectionMatrix should compute all three matrices
    auto viewProjMatrix = pCamera->GetViewProjectionMatrix();

    // Now calling individual getters should return cached values (not recompute)
    auto viewMatrix = pCamera->GetViewMatrix();
    auto projMatrix = pCamera->GetProjectionMatrix();

    // Verify view-projection is the correct product
    auto expectedViewProj = viewMatrix * projMatrix;
    EXPECT_TRUE(AlmostEqual(viewProjMatrix, expectedViewProj));

    // Test: Calling individual getters first, then combined
    pCameraNode->SetLocalTranslation(FloatVector4(20.0f, 10.0f, 0.0f, 1.0f));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    auto viewMatrix2 = pCamera->GetViewMatrix();
    auto projMatrix2 = pCamera->GetProjectionMatrix();
    auto viewProjMatrix2 = pCamera->GetViewProjectionMatrix();

    // Verify consistency
    auto expectedViewProj2 = viewMatrix2 * projMatrix2;
    EXPECT_TRUE(AlmostEqual(viewProjMatrix2, expectedViewProj2));

    // Test: Multiple calls without changes should return same matrices
    auto viewMatrix3 = pCamera->GetViewMatrix();
    auto projMatrix3 = pCamera->GetProjectionMatrix();
    auto viewProjMatrix3 = pCamera->GetViewProjectionMatrix();

    EXPECT_TRUE(AlmostEqual(viewMatrix2, viewMatrix3));
    EXPECT_TRUE(AlmostEqual(projMatrix2, projMatrix3));
    EXPECT_TRUE(AlmostEqual(viewProjMatrix2, viewProjMatrix3));
}

TEST(CanvasInterfacesTest, LightTest)
{
    using namespace Canvas::Math;

    // Create Canvas and Scene
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // Create a hierarchy: root -> A -> B -> lightNode
    Gem::TGemPtr<XSceneGraphNode> pA, pB, pLightNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pA)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pB)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pLightNode)));

    Gem::TGemPtr<XSceneGraphNode> pRoot = pWorld->GetRootNode();
    EXPECT_NE(pRoot.Get(), nullptr);
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pA)));
    EXPECT_TRUE(Succeeded(pA->AddChild(pB)));
    EXPECT_TRUE(Succeeded(pB->AddChild(pLightNode)));

    // Create light and attach to lightNode
    Gem::TGemPtr<XLight> pLight;
    EXPECT_TRUE(Succeeded(pCanvas->CreateLight(LightType::Point, &pLight)));
    EXPECT_TRUE(Succeeded(pLightNode->BindElement(pLight)));

    // Verify light is attached to correct node
    EXPECT_TRUE(pLight->GetAttachedNode() == pLightNode.Get());

    // Set transforms for the hierarchy
    // A: rotate 90° around Z, translate (5,0,0)
    const float ninety = float(3.14159265358979323846 / 2.0);
    auto qA = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, ninety);
    pA->SetLocalRotation(qA);
    pA->SetLocalTranslation(FloatVector4(5.0f, 0.0f, 0.0f, 1.0f));

    // B: rotate 90° around Y, translate (0,3,0)
    auto qB = FloatQuaternion::FromEulerXYZ(0.0f, ninety, 0.0f);
    pB->SetLocalRotation(qB);
    pB->SetLocalTranslation(FloatVector4(0.0f, 3.0f, 0.0f, 1.0f));

    // LightNode: translate (2,0,0)
    pLightNode->SetLocalTranslation(FloatVector4(2.0f, 0.0f, 0.0f, 1.0f));

    // Update scene to compute global transforms
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    // Verify light's world transform through its attached node
    auto lightWorldMatrix = pLightNode->GetGlobalMatrix();
    auto lightWorldTranslation = pLightNode->GetGlobalTranslation();

    // Light position should reflect the full hierarchy transform
    // This verifies the light properly inherits ancestor transforms
    EXPECT_TRUE(lightWorldTranslation.W == 1.0f);

    // Change ancestor transform and verify light position updates
    pA->SetLocalTranslation(FloatVector4(10.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    auto lightWorldTranslation2 = pLightNode->GetGlobalTranslation();

    // Light position should have changed due to ancestor change
    EXPECT_FALSE(AlmostEqual(lightWorldTranslation, lightWorldTranslation2));
}

TEST(CanvasInterfacesTest, LightNodeReparenting)
{
    using namespace Canvas::Math;

    // Create Canvas and Scene
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // Create nodes: A, B, and lightNode
    Gem::TGemPtr<XSceneGraphNode> pA, pB, pLightNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pA)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pB)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pLightNode)));

    Gem::TGemPtr<XSceneGraphNode> pRoot = pWorld->GetRootNode();

    // Initial hierarchy: root -> A -> lightNode
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pA)));
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pB)));
    EXPECT_TRUE(Succeeded(pA->AddChild(pLightNode)));

    // Create and attach light
    Gem::TGemPtr<XLight> pLight;
    EXPECT_TRUE(Succeeded(pCanvas->CreateLight(LightType::Spot, &pLight)));
    EXPECT_TRUE(Succeeded(pLightNode->BindElement(pLight)));

    // Set different transforms for A and B
    const float ninety = float(3.14159265358979323846 / 2.0);
    auto qA = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, ninety); // Rotate 90° around Z
    pA->SetLocalRotation(qA);
    pA->SetLocalTranslation(FloatVector4(5.0f, 0.0f, 0.0f, 1.0f));

    auto qB = FloatQuaternion::FromEulerXYZ(0.0f, ninety, 0.0f); // Rotate 90° around Y
    pB->SetLocalRotation(qB);
    pB->SetLocalTranslation(FloatVector4(0.0f, 5.0f, 0.0f, 1.0f));

    pLightNode->SetLocalTranslation(FloatVector4(1.0f, 0.0f, 0.0f, 1.0f));

    // Get initial light world transform (under A)
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));
    auto lightTranslation1 = pLightNode->GetGlobalTranslation();
    auto lightRotation1 = pLightNode->GetGlobalRotation();

    // Move light node from A to B
    EXPECT_TRUE(Succeeded(pB->AddChild(pLightNode)));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    // Light's world transform should be different (different parent)
    auto lightTranslation2 = pLightNode->GetGlobalTranslation();
    auto lightRotation2 = pLightNode->GetGlobalRotation();

    EXPECT_FALSE(AlmostEqual(lightTranslation1, lightTranslation2));
    EXPECT_FALSE(AlmostEqual(lightRotation1, lightRotation2));

    // Move light to root (no parent transform)
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pLightNode)));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    auto lightTranslation3 = pLightNode->GetGlobalTranslation();
    auto lightRotation3 = pLightNode->GetGlobalRotation();

    // Under root, light's world transform should equal its local transform
    auto lightLocalTranslation = pLightNode->GetLocalTranslation();
    auto lightLocalRotation = pLightNode->GetLocalRotation();

    EXPECT_TRUE(AlmostEqual(lightTranslation3, lightLocalTranslation));
    EXPECT_TRUE(AlmostEqual(lightRotation3, lightLocalRotation));
}

TEST(CanvasInterfacesTest, LightTransformPropagation)
{
    using namespace Canvas::Math;

    // Create Canvas and Scene
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // Create hierarchy: root -> parent -> lightNode
    Gem::TGemPtr<XSceneGraphNode> pParent, pLightNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pParent)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pLightNode)));

    Gem::TGemPtr<XSceneGraphNode> pRoot = pWorld->GetRootNode();
    EXPECT_TRUE(Succeeded(pRoot->AddChild(pParent)));
    EXPECT_TRUE(Succeeded(pParent->AddChild(pLightNode)));

    // Create and attach light
    Gem::TGemPtr<XLight> pLight;
    EXPECT_TRUE(Succeeded(pCanvas->CreateLight(LightType::Directional, &pLight)));
    EXPECT_TRUE(Succeeded(pLightNode->BindElement(pLight)));

    // Set initial transforms
    const float ninety = float(3.14159265358979323846 / 2.0);
    auto qParent = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, ninety);
    pParent->SetLocalRotation(qParent);
    pParent->SetLocalTranslation(FloatVector4(5.0f, 5.0f, 0.0f, 1.0f));
    pLightNode->SetLocalTranslation(FloatVector4(2.0f, 0.0f, 0.0f, 1.0f));

    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));
    auto lightTranslation1 = pLightNode->GetGlobalTranslation();

    // Change parent's translation - light should update
    pParent->SetLocalTranslation(FloatVector4(10.0f, 10.0f, 0.0f, 1.0f));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    auto lightTranslation2 = pLightNode->GetGlobalTranslation();
    EXPECT_FALSE(AlmostEqual(lightTranslation1, lightTranslation2));

    // Verify the light's world transform is computed correctly
    // Row vectors: world = light_local * parent_global
    auto parentGlobal = pParent->GetGlobalMatrix();
    auto lightLocal = pLightNode->GetLocalMatrix();
    auto expectedLightGlobal = lightLocal * parentGlobal;
    auto actualLightGlobal = pLightNode->GetGlobalMatrix();
    EXPECT_TRUE(AlmostEqual(expectedLightGlobal, actualLightGlobal));

    // Change parent's rotation - light should update
    auto qParentNew = FloatQuaternion::FromEulerXYZ(0.0f, ninety, 0.0f);
    pParent->SetLocalRotation(qParentNew);
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));

    auto lightTranslation3 = pLightNode->GetGlobalTranslation();
    EXPECT_FALSE(AlmostEqual(lightTranslation2, lightTranslation3));

    // Verify correctness again after rotation change
    auto parentGlobal2 = pParent->GetGlobalMatrix();
    auto expectedLightGlobal2 = lightLocal * parentGlobal2;
    auto actualLightGlobal2 = pLightNode->GetGlobalMatrix();
    EXPECT_TRUE(AlmostEqual(expectedLightGlobal2, actualLightGlobal2));
}

TEST(CanvasInterfacesTest, LightProperties)
{
    using namespace Canvas::Math;

    // Create Canvas
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    // Test Point Light
    Gem::TGemPtr<XLight> pPointLight;
    EXPECT_TRUE(Succeeded(pCanvas->CreateLight(LightType::Point, &pPointLight)));

    // Verify immutable type
    EXPECT_TRUE(pPointLight->GetType() == LightType::Point);

    // Test Color
    FloatVector4 testColor(1.0f, 0.5f, 0.25f, 1.0f);
    pPointLight->SetColor(testColor);
    auto retrievedColor = pPointLight->GetColor();
    EXPECT_TRUE(AlmostEqual(retrievedColor, testColor));

    // Test Intensity
    pPointLight->SetIntensity(2.5f);
    EXPECT_EQ(2.5f, pPointLight->GetIntensity());

    // Test Flags
    pPointLight->SetFlags(LightFlags::CastsShadows | LightFlags::Enabled);
    EXPECT_EQ(LightFlags::CastsShadows | LightFlags::Enabled, pPointLight->GetFlags());

    // Test Range
    pPointLight->SetRange(500.0f);
    EXPECT_EQ(500.0f, pPointLight->GetRange());

    // Test Attenuation
    pPointLight->SetAttenuation(1.0f, 0.09f, 0.032f);
    float constant = 0.0f, linear = 0.0f, quadratic = 0.0f;
    pPointLight->GetAttenuation(&constant, &linear, &quadratic);
    EXPECT_EQ(1.0f, constant);
    EXPECT_EQ(0.09f, linear);
    EXPECT_EQ(0.032f, quadratic);

    // Test Spot Light
    Gem::TGemPtr<XLight> pSpotLight;
    EXPECT_TRUE(Succeeded(pCanvas->CreateLight(LightType::Spot, &pSpotLight)));

    // Verify type
    EXPECT_TRUE(pSpotLight->GetType() == LightType::Spot);

    // Test Spot Angles
    const float innerAngle = 0.523599f; // 30 degrees
    const float outerAngle = 0.785398f; // 45 degrees
    pSpotLight->SetSpotAngles(innerAngle, outerAngle);
    float retrievedInner = 0.0f, retrievedOuter = 0.0f;
    pSpotLight->GetSpotAngles(&retrievedInner, &retrievedOuter);
    EXPECT_EQ(innerAngle, retrievedInner);
    EXPECT_EQ(outerAngle, retrievedOuter);

    // Test Directional Light
    Gem::TGemPtr<XLight> pDirectionalLight;
    EXPECT_TRUE(Succeeded(pCanvas->CreateLight(LightType::Directional, &pDirectionalLight)));
    EXPECT_TRUE(pDirectionalLight->GetType() == LightType::Directional);

    // Directional lights can still have color and intensity
    FloatVector4 sunColor(1.0f, 0.95f, 0.8f, 1.0f);
    pDirectionalLight->SetColor(sunColor);
    pDirectionalLight->SetIntensity(1.5f);
    EXPECT_TRUE(AlmostEqual(pDirectionalLight->GetColor(), sunColor));
    EXPECT_EQ(1.5f, pDirectionalLight->GetIntensity());

    // Test Ambient Light
    Gem::TGemPtr<XLight> pAmbientLight;
    EXPECT_TRUE(Succeeded(pCanvas->CreateLight(LightType::Ambient, &pAmbientLight)));
    EXPECT_TRUE(pAmbientLight->GetType() == LightType::Ambient);

    // Ambient light should support color and intensity
    FloatVector4 ambientColor(0.2f, 0.2f, 0.3f, 1.0f);
    pAmbientLight->SetColor(ambientColor);
    pAmbientLight->SetIntensity(0.5f);
    EXPECT_TRUE(AlmostEqual(pAmbientLight->GetColor(), ambientColor));
    EXPECT_EQ(0.5f, pAmbientLight->GetIntensity());
}

// ============================================================
// XModel Tests
// ============================================================
namespace {

using Canvas::InvalidIndex;

Canvas::Transform Translation(float x, float y, float z)
{
    Canvas::Transform t;
    t.Translation = Canvas::Math::FloatVector4(x, y, z, 1.0f);
    return t;
}

// Adds a scene node animation target of the given type for a model node and returns its index.
uint32_t AddNodeTarget(Canvas::XModelBuilder *pBuilder, uint32_t node, Canvas::AnimationTargetType type)
{
    Canvas::AnimationTargetDesc target{};
    target.Type  = type;
    target.Index = node;
    uint32_t index = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationTarget(&target, &index)));
    return index;
}

// Adds a Value animation target and returns its index.
uint32_t AddValueTarget(Canvas::XModelBuilder *pBuilder, Canvas::AnimationValueType valueType)
{
    Canvas::AnimationTargetDesc target{};
    target.Type      = Canvas::AnimationTargetType::Value;
    target.ValueType = valueType;
    uint32_t index = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationTarget(&target, &index)));
    return index;
}

// A track with count keys; pValues holds count packed values of the given type.
Canvas::AnimationTrackDesc Track(Canvas::AnimationValueType valueType, const float *pTimes, const float *pValues,
    uint32_t count, Canvas::AnimationInterpolation interpolation = Canvas::AnimationInterpolation::Linear)
{
    Canvas::AnimationTrackDesc track{};
    track.ValueType     = valueType;
    track.Interpolation = interpolation;
    track.pTimes        = pTimes;
    track.pValues       = pValues;
    track.KeyCount      = count;
    return track;
}

// A clip whose track i drives target pTargets[i].
Canvas::AnimationClipDesc Clip(const char *name, float duration,
    const Canvas::AnimationTrackDesc *pTracks, const uint32_t *pTargets, uint32_t count)
{
    Canvas::AnimationClipDesc clip{};
    clip.Name          = name;
    clip.Duration      = duration;
    clip.pTracks       = pTracks;
    clip.pTrackTargets = pTargets;
    clip.TrackCount    = count;
    return clip;
}

// A new builder, already Building a model with the given name.
Gem::TGemPtr<Canvas::XModelBuilder> BeginModel(XCanvas *pCanvas, Canvas::XGfxDevice *pDevice, const char *name)
{
    Gem::TGemPtr<Canvas::XModelBuilder> pBuilder;
    EXPECT_TRUE(Succeeded(pCanvas->CreateModelBuilder(pDevice, &pBuilder, "TestBuilder")));
    if (pBuilder)
        EXPECT_TRUE(Succeeded(pBuilder->Begin(name)));
    return pBuilder;
}

uint32_t AddNode(Canvas::XModelBuilder *pBuilder, const char *name, uint32_t parent,
                 const Canvas::Transform &transform = {})
{
    Canvas::ModelNodeDesc desc{};
    desc.Name        = name;
    desc.ParentIndex = parent;
    desc.LocalTransform   = transform;
    uint32_t index = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddNode(&desc, &index)));
    return index;
}

Gem::TGemPtr<Canvas::XGfxMeshData> CreateTriangle(Canvas::XGfxDevice *pDevice, const char *name)
{
    using namespace Canvas::Math;
    FloatVector4 positions[] = { {0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1} };
    FloatVector4 normals[]   = { {0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0} };
    Gem::TGemPtr<Canvas::XGfxMeshData> pMesh;
    EXPECT_TRUE(Succeeded(pDevice->CreateDebugMeshData(3, positions, normals, &pMesh, name)));
    return pMesh;
}

Gem::TGemPtr<Canvas::XModel> BuildModel(Canvas::XModelBuilder *pBuilder)
{
    Gem::TGemPtr<Canvas::XModel> pModel;
    EXPECT_TRUE(Succeeded(pBuilder->Build(&pModel)));
    return pModel;
}

// The first element of type T bound to pNode, or null.  The node keeps it alive.
template<class T>
T *FindElement(Canvas::XSceneGraphNode *pNode)
{
    for (uint32_t i = 0; i < pNode->GetBoundElementCount(); ++i)
    {
        Gem::TGemPtr<T> p;
        if (Succeeded(pNode->GetBoundElement(i)->QueryInterface(&p)))
            return p.Get();
    }
    return nullptr;
}

} // anonymous namespace

TEST(CanvasInterfacesTest, ModelBuilderLifecycle)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<Canvas::XModelBuilder> pBuilder;
    EXPECT_TRUE(Succeeded(pCanvas->CreateModelBuilder(pDevice, &pBuilder, "Builder")));
    ASSERT_NE(pBuilder.Get(), nullptr);
    EXPECT_TRUE(pBuilder->GetDevice() == pDevice.Get());
    EXPECT_FALSE(pBuilder->IsBuilding());

    // Uninitialized: content and Build are refused.
    Canvas::ModelNodeDesc node{};
    node.Name = "Root";
    EXPECT_EQ(Gem::Result::Uninitialized, pBuilder->AddNode(&node, nullptr));
    Gem::TGemPtr<Canvas::XModel> pModel;
    EXPECT_EQ(Gem::Result::Uninitialized, pBuilder->Build(&pModel));
    EXPECT_EQ(pModel.Get(), nullptr);

    EXPECT_TRUE(Succeeded(pBuilder->Begin("First")));
    EXPECT_TRUE(pBuilder->IsBuilding());
    EXPECT_EQ(Gem::Result::AlreadyExists, pBuilder->Begin("Again"));

    // A model has at least one model node; a failed Build keeps building.
    EXPECT_TRUE(Gem::Failed(pBuilder->Build(&pModel)));
    EXPECT_EQ(pModel.Get(), nullptr);
    EXPECT_TRUE(pBuilder->IsBuilding());

    uint32_t index = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddNode(&node, &index)));
    EXPECT_EQ(0u, index);
    EXPECT_TRUE(Succeeded(pBuilder->Build(&pModel)));
    ASSERT_NE(pModel.Get(), nullptr);
    EXPECT_STREQ("First", pModel->GetName());
    EXPECT_TRUE(pModel->GetDevice() == pDevice.Get());
    EXPECT_EQ(1u, pModel->GetNodeCount());

    // A successful Build returns the builder to Uninitialized, so a second Build fails
    // rather than producing another model.
    EXPECT_FALSE(pBuilder->IsBuilding());
    Gem::TGemPtr<Canvas::XModel> pSecond;
    EXPECT_EQ(Gem::Result::Uninitialized, pBuilder->Build(&pSecond));
    EXPECT_EQ(pSecond.Get(), nullptr);

    // The next model starts empty, and Reset discards an unfinished one.
    EXPECT_TRUE(Succeeded(pBuilder->Begin("Second")));
    EXPECT_TRUE(Succeeded(pBuilder->AddNode(&node, &index)));
    EXPECT_EQ(0u, index);
    pBuilder->Reset();
    EXPECT_FALSE(pBuilder->IsBuilding());
    EXPECT_EQ(Gem::Result::Uninitialized, pBuilder->Build(&pSecond));

    // Later use of the builder leaves the first model unchanged.
    EXPECT_EQ(1u, pModel->GetNodeCount());
    EXPECT_STREQ("Root", pModel->GetNodeName(0));
}

TEST(CanvasInterfacesTest, ModelResourceLibrary)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "ResourceModel");
    ASSERT_NE(pBuilder.Get(), nullptr);

    auto pMesh = CreateTriangle(pDevice, "TriMesh");
    uint32_t meshIndex = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddMeshData(pMesh, &meshIndex)));
    EXPECT_EQ(0u, meshIndex);

    Gem::TGemPtr<Canvas::XGfxMaterial> pMaterial;
    EXPECT_TRUE(Succeeded(pDevice->CreateMaterial(&pMaterial)));
    EXPECT_TRUE(Succeeded(pBuilder->AddMaterial(pMaterial, nullptr)));

    // Null resources are rejected.
    EXPECT_TRUE(Gem::Failed(pBuilder->AddMeshData(nullptr, nullptr)));
    EXPECT_TRUE(Gem::Failed(pBuilder->AddMaterial(nullptr, nullptr)));
    EXPECT_TRUE(Gem::Failed(pBuilder->AddTexture(nullptr, nullptr)));

    // A mesh names mesh data that the model holds.
    const uint32_t node = AddNode(pBuilder, "Node", InvalidIndex, Translation(5.0f, 0.0f, 0.0f));
    Canvas::ModelMeshDesc mesh{};
    mesh.NodeIndex     = node;
    mesh.MeshDataIndex = 1;
    EXPECT_EQ(Gem::Result::OutOfRange, pBuilder->AddMesh(&mesh, nullptr));
    mesh.MeshDataIndex = meshIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddMesh(&mesh, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);
    EXPECT_EQ(1u, pModel->GetMeshDataCount());
    EXPECT_TRUE(pModel->GetMeshData(0) == pMesh.Get());
    EXPECT_EQ(pModel->GetMeshData(1), nullptr); // out of range
    EXPECT_EQ(1u, pModel->GetMaterialCount());
    EXPECT_TRUE(pModel->GetMaterial(0) == pMaterial.Get());
    EXPECT_EQ(0u, pModel->GetTextureCount());
    EXPECT_EQ(0u, pModel->GetCameraCount());
    EXPECT_EQ(InvalidIndex, pModel->GetActiveCameraIndex());
    EXPECT_EQ(0u, pModel->GetSkeletonCount());
    EXPECT_EQ(0u, pModel->GetAnimationClipCount());

    // Bounds enclose the triangle where its node places it.
    const AABB bounds = pModel->GetBounds();
    EXPECT_FALSE(bounds.IsEmpty());
    EXPECT_NEAR(5.0f, bounds.Min.X, 1e-5f);
    EXPECT_NEAR(6.0f, bounds.Max.X, 1e-5f);
    EXPECT_NEAR(1.0f, bounds.Max.Y, 1e-5f);
}

TEST(CanvasInterfacesTest, ModelBuilderValidatesIndices)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "Indices");
    ASSERT_NE(pBuilder.Get(), nullptr);

    // Model nodes are listed parents first.
    Canvas::ModelNodeDesc orphan{};
    orphan.ParentIndex = 0;
    EXPECT_EQ(Gem::Result::OutOfRange, pBuilder->AddNode(&orphan, nullptr));
    const uint32_t root = AddNode(pBuilder, "Root", InvalidIndex);

    Canvas::ModelCameraDesc camera{};
    camera.NodeIndex = 5;
    EXPECT_EQ(Gem::Result::OutOfRange, pBuilder->AddCamera(&camera, nullptr));
    Canvas::ModelLightDesc light{};
    light.NodeIndex = 5;
    EXPECT_EQ(Gem::Result::OutOfRange, pBuilder->AddLight(&light, nullptr));
    EXPECT_EQ(Gem::Result::OutOfRange, pBuilder->SetActiveCamera(0));

    // Bones are listed parents first.
    Canvas::ModelBoneDesc bones[2] = {};
    bones[0].ParentIndex = 1;
    Canvas::ModelSkeletonDesc skeleton{};
    skeleton.NodeIndex = root;
    skeleton.BoneCount = 2;
    skeleton.pBones    = bones;
    EXPECT_EQ(Gem::Result::OutOfRange, pBuilder->AddSkeleton(&skeleton, nullptr));
    bones[0].ParentIndex = InvalidIndex;
    bones[1].ParentIndex = 0;
    uint32_t skeletonIndex = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddSkeleton(&skeleton, &skeletonIndex)));

    Canvas::ModelSocketDesc socket{};
    socket.SkeletonIndex = skeletonIndex;
    socket.BoneIndex     = 2;
    EXPECT_EQ(Gem::Result::OutOfRange, pBuilder->AddSocket(&socket, nullptr));

    // A skinned mesh binds bones of its skeleton.
    uint32_t meshIndex = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddMeshData(CreateTriangle(pDevice, "Tri"), &meshIndex)));
    const uint32_t badBone = 2;
    const FloatMatrix4x4 invBind = FloatMatrix4x4::Identity();
    Canvas::ModelMeshDesc mesh{};
    mesh.NodeIndex            = root;
    mesh.MeshDataIndex        = meshIndex;
    mesh.SkeletonIndex        = skeletonIndex;
    mesh.BoneCount            = 1;
    mesh.pSkeletonBoneIndices = &badBone;
    mesh.pInvBindPoses        = &invBind;
    EXPECT_EQ(Gem::Result::OutOfRange, pBuilder->AddMesh(&mesh, nullptr));

    // Skin data names a skeleton.
    mesh.SkeletonIndex = InvalidIndex;
    EXPECT_EQ(Gem::Result::InvalidArg, pBuilder->AddMesh(&mesh, nullptr));

    // Build checks animation target transforms, and a failed Build keeps the model's content.
    EXPECT_EQ(0u, AddNodeTarget(pBuilder, 7, Canvas::AnimationTargetType::SceneNodeTranslation));

    Gem::TGemPtr<Canvas::XModel> pModel;
    EXPECT_EQ(Gem::Result::OutOfRange, pBuilder->Build(&pModel));
    EXPECT_EQ(pModel.Get(), nullptr);
    EXPECT_TRUE(pBuilder->IsBuilding());
    uint32_t next = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddNode(&orphan, &next)));    // parent 0 now exists
    EXPECT_EQ(1u, next);
}

TEST(CanvasInterfacesTest, ModelAuthoringAndInstantiation)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    // Build a model with a simple hierarchy:
    //   - NodeA (mesh)
    //   - NodeB
    //       - NodeC (mesh)
    auto pBuilder = BeginModel(pCanvas, pDevice, "TestModel");
    ASSERT_NE(pBuilder.Get(), nullptr);

    auto pMesh = CreateTriangle(pDevice, "SharedMesh");
    uint32_t meshIndex = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddMeshData(pMesh, &meshIndex)));

    const uint32_t nodeA = AddNode(pBuilder, "NodeA", InvalidIndex, Translation(1.0f, 0.0f, 0.0f));
    const uint32_t nodeB = AddNode(pBuilder, "NodeB", InvalidIndex, Translation(0.0f, 2.0f, 0.0f));
    const uint32_t nodeC = AddNode(pBuilder, "NodeC", nodeB, Translation(0.0f, 0.0f, 3.0f));

    Canvas::ModelMeshDesc mesh{};
    mesh.MeshDataIndex = meshIndex;
    mesh.Name = "MeshA"; mesh.NodeIndex = nodeA;
    EXPECT_TRUE(Succeeded(pBuilder->AddMesh(&mesh, nullptr)));
    mesh.Name = "MeshC"; mesh.NodeIndex = nodeC;
    EXPECT_TRUE(Succeeded(pBuilder->AddMesh(&mesh, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);

    // The model's node table, parents first
    EXPECT_EQ(3u, pModel->GetNodeCount());
    EXPECT_STREQ("NodeC", pModel->GetNodeName(nodeC));
    EXPECT_EQ(nodeB, pModel->GetNodeParent(nodeC));
    EXPECT_EQ(InvalidIndex, pModel->GetNodeParent(nodeA));
    Canvas::Transform transform;
    EXPECT_TRUE(Succeeded(pModel->GetNodeTransform(nodeC, &transform)));
    EXPECT_TRUE(AlmostEqual(transform.Translation, FloatVector4(0.0f, 0.0f, 3.0f, 1.0f)));
    EXPECT_EQ(Gem::Result::InvalidArg, pModel->GetNodeTransform(3, &transform));

    // Instantiate into a scene
    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "TestWorld")));

    Canvas::ModelInstantiateResult result{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));

    // Verify instance root was created
    EXPECT_NE(result.pInstanceRoot, nullptr);
    EXPECT_EQ(result.pActiveCamera, nullptr); // no camera in model
    EXPECT_EQ(result.pAnimationController, nullptr); // no clips in model

    // Instance root should be a child of scene root
    Canvas::XSceneGraphNode *pSceneRoot = pWorld->GetRootNode();
    EXPECT_TRUE(pSceneRoot->GetFirstChild() == result.pInstanceRoot);

    // Instance root should have 2 children (NodeA and NodeB)
    Canvas::XSceneGraphNode *pNodeA = result.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pNodeA, nullptr);
    Canvas::XSceneGraphNode *pNodeB = result.pInstanceRoot->GetNextChild(pNodeA);
    ASSERT_NE(pNodeB, nullptr);
    EXPECT_EQ(result.pInstanceRoot->GetNextChild(pNodeB), nullptr); // no more children
    EXPECT_STREQ("NodeA", pNodeA->GetName());
    EXPECT_STREQ("NodeB", pNodeB->GetName());

    // Transforms come from the model nodes
    EXPECT_TRUE(AlmostEqual(pNodeA->GetLocalTranslation(), FloatVector4(1.0f, 0.0f, 0.0f, 1.0f)));
    EXPECT_TRUE(AlmostEqual(pNodeB->GetLocalTranslation(), FloatVector4(0.0f, 2.0f, 0.0f, 1.0f)));

    // NodeB should have one child (NodeC)
    Canvas::XSceneGraphNode *pNodeC = pNodeB->GetFirstChild();
    ASSERT_NE(pNodeC, nullptr);
    EXPECT_TRUE(AlmostEqual(pNodeC->GetLocalTranslation(), FloatVector4(0.0f, 0.0f, 3.0f, 1.0f)));

    // Mesh instances share the model's GPU mesh data
    EXPECT_EQ(1u, pNodeA->GetBoundElementCount());
    Canvas::XMeshInstance *pMeshA = FindElement<Canvas::XMeshInstance>(pNodeA);
    ASSERT_NE(pMeshA, nullptr);
    EXPECT_STREQ("MeshA", pMeshA->GetName());
    EXPECT_TRUE(pMeshA->GetMeshData() == pMesh.Get());

    EXPECT_EQ(1u, pNodeC->GetBoundElementCount());
    Canvas::XMeshInstance *pMeshC = FindElement<Canvas::XMeshInstance>(pNodeC);
    ASSERT_NE(pMeshC, nullptr);
    EXPECT_TRUE(pMeshC->GetMeshData() == pMesh.Get());
    EXPECT_EQ(0u, pMeshC->GetSkinBoneCount());
}

TEST(CanvasInterfacesTest, ModelInstantiateTransformIndependence)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "TestModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    AddNode(pBuilder, "NodeA", InvalidIndex, Translation(1.0f, 0.0f, 0.0f));
    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);

    // Create scene and instantiate twice
    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "TestWorld")));

    Canvas::ModelInstantiateResult result1{}, result2{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result1)));
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result2)));

    // Both instances should exist independently
    EXPECT_NE(result1.pInstanceRoot, nullptr);
    EXPECT_NE(result2.pInstanceRoot, nullptr);
    EXPECT_TRUE(result1.pInstanceRoot != result2.pInstanceRoot);

    // Modify instance 1's child transform
    Canvas::XSceneGraphNode *pNode1A = result1.pInstanceRoot->GetFirstChild();
    Canvas::XSceneGraphNode *pNode2A = result2.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pNode1A, nullptr);
    ASSERT_NE(pNode2A, nullptr);

    pNode1A->SetLocalTranslation(FloatVector4(99.0f, 0.0f, 0.0f, 0.0f));

    // Instance 2 and the model are unaffected
    EXPECT_TRUE(AlmostEqual(pNode2A->GetLocalTranslation(), FloatVector4(1.0f, 0.0f, 0.0f, 1.0f)));
    Canvas::Transform transform;
    EXPECT_TRUE(Succeeded(pModel->GetNodeTransform(0, &transform)));
    EXPECT_TRUE(AlmostEqual(transform.Translation, FloatVector4(1.0f, 0.0f, 0.0f, 1.0f)));
}

TEST(CanvasInterfacesTest, ModelInstantiateCopiesTransforms)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Canvas::Transform transform;
    transform.Translation = FloatVector4(1.0f, 2.0f, 3.0f, 0.0f);
    transform.Rotation    = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, 0.5f);
    transform.Scale       = FloatVector4(100.0f, 2.0f, 0.5f, 0.0f);

    auto pBuilder = BeginModel(pCanvas, pDevice, "TestModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    AddNode(pBuilder, "Scaled", InvalidIndex, transform);
    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "TestWorld")));

    Canvas::ModelInstantiateResult result{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));
    Canvas::XSceneGraphNode *pNode = result.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pNode, nullptr);

    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(), transform.Translation));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalRotation(), transform.Rotation));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalScale(), transform.Scale));
}

TEST(CanvasInterfacesTest, ModelActiveCameraInstantiation)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "CameraModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t camNode = AddNode(pBuilder, "CamNode", InvalidIndex, Translation(0.0f, -5.0f, 2.0f));

    Canvas::ModelCameraDesc camera{};
    camera.Name             = "ModelCamera";
    camera.NodeIndex        = camNode;
    camera.Params.FovAngle  = 1.2f;
    camera.Params.NearClip  = 0.5f;
    camera.Params.FarClip   = 500.0f;
    uint32_t cameraIndex = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddCamera(&camera, &cameraIndex)));
    EXPECT_TRUE(Succeeded(pBuilder->SetActiveCamera(cameraIndex)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);
    EXPECT_EQ(1u, pModel->GetCameraCount());
    EXPECT_EQ(cameraIndex, pModel->GetActiveCameraIndex());

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "TestWorld")));

    Canvas::ModelInstantiateResult result{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));

    // The instance's active camera carries the model camera's properties
    ASSERT_NE(result.pActiveCamera, nullptr);
    EXPECT_STREQ("ModelCamera", result.pActiveCamera->GetName());
    EXPECT_EQ(1.2f, result.pActiveCamera->GetFovAngle());
    EXPECT_EQ(0.5f, result.pActiveCamera->GetNearClip());
    EXPECT_EQ(500.0f, result.pActiveCamera->GetFarClip());

    // ...and is attached to the instance's scene node for the camera's model node
    Canvas::XSceneGraphNode *pCamNode = result.pActiveCamera->GetAttachedNode();
    ASSERT_NE(pCamNode, nullptr);
    EXPECT_TRUE(pCamNode == result.pInstanceRoot->GetFirstChild());
    EXPECT_TRUE(AlmostEqual(pCamNode->GetLocalTranslation(), FloatVector4(0.0f, -5.0f, 2.0f, 1.0f)));
}

TEST(CanvasInterfacesTest, ModelLightInstantiation)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "LightModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t lightNode = AddNode(pBuilder, "LightNode", InvalidIndex);

    Canvas::ModelLightDesc light{};
    light.Name             = "PointLight";
    light.NodeIndex        = lightNode;
    light.Type             = Canvas::LightType::Point;
    light.Params.Color     = FloatVector4(1.0f, 0.5f, 0.0f, 1.0f);
    light.Params.Intensity = 3.0f;
    light.Params.Range     = 50.0f;
    EXPECT_TRUE(Succeeded(pBuilder->AddLight(&light, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "TestWorld")));

    Canvas::ModelInstantiateResult result{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));

    Canvas::XSceneGraphNode *pLightNode = result.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pLightNode, nullptr);
    EXPECT_EQ(1u, pLightNode->GetBoundElementCount());

    Canvas::XLight *pLight = FindElement<Canvas::XLight>(pLightNode);
    ASSERT_NE(pLight, nullptr);
    EXPECT_TRUE(pLight->GetType() == Canvas::LightType::Point);
    EXPECT_TRUE(AlmostEqual(pLight->GetColor(), FloatVector4(1.0f, 0.5f, 0.0f, 1.0f)));
    EXPECT_EQ(3.0f, pLight->GetIntensity());
    EXPECT_EQ(50.0f, pLight->GetRange());
    EXPECT_TRUE(pLight->GetFlags() == Canvas::LightFlags::Enabled);
}

TEST(CanvasInterfacesTest, ModelInstantiateNullTargetFails)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "TestModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    AddNode(pBuilder, "Node", InvalidIndex);
    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);

    // Null target should fail
    EXPECT_TRUE(Gem::Failed(pModel->Instantiate(nullptr)));
}

// A skeleton instance composes its bones parents first, in the space of its node, and
// moves its sockets (and the content under them) with their bones.  Scene-graph nodes
// with the same transforms serve as the reference composition.
TEST(CanvasInterfacesTest, SkeletonInstancePoseAndSockets)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "Rig");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t armature = AddNode(pBuilder, "Armature", InvalidIndex, Translation(10.0f, 0.0f, 0.0f));

    Canvas::ModelBoneDesc bones[2] = {};
    bones[0].Name          = "Hip";
    bones[0].RestTransform = Translation(0.0f, 0.0f, 1.0f);
    bones[1].Name          = "Knee";
    bones[1].ParentIndex   = 0;
    bones[1].RestTransform = Translation(0.0f, 0.0f, -0.5f);
    bones[1].RestTransform.Rotation = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, 0.5f);

    Canvas::ModelSkeletonDesc skeleton{};
    skeleton.Name      = "Legs";
    skeleton.NodeIndex = armature;
    skeleton.BoneCount = 2;
    skeleton.pBones    = bones;
    uint32_t skeletonIndex = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddSkeleton(&skeleton, &skeletonIndex)));

    Canvas::ModelSocketDesc socket{};
    socket.Name           = "KneeSocket";
    socket.SkeletonIndex  = skeletonIndex;
    socket.BoneIndex      = 1;
    socket.LocalTransform = Translation(1.0f, 0.0f, 0.0f);
    uint32_t socketNode = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddSocket(&socket, &socketNode)));
    AddNode(pBuilder, "Prop", socketNode);

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);
    EXPECT_EQ(1u, pModel->GetSkeletonCount());
    EXPECT_EQ(3u, pModel->GetNodeCount());
    EXPECT_EQ(armature, pModel->GetNodeParent(socketNode));

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "TestWorld")));
    Canvas::ModelInstantiateResult result{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));

    Canvas::XSceneGraphNode *pArmature = result.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pArmature, nullptr);
    Canvas::XSceneGraphNode *pSocket = pArmature->GetFirstChild();
    ASSERT_NE(pSocket, nullptr);
    EXPECT_STREQ("KneeSocket", pSocket->GetName());
    Canvas::XSceneGraphNode *pProp = pSocket->GetFirstChild();
    ASSERT_NE(pProp, nullptr);

    // Bones are not scene nodes: the skeleton instance on the armature holds them.
    Canvas::XSkeletonInstance *pSkeleton = FindElement<Canvas::XSkeletonInstance>(pArmature);
    ASSERT_NE(pSkeleton, nullptr);
    EXPECT_EQ(2u, pSkeleton->GetBoneCount());
    EXPECT_STREQ("Hip", pSkeleton->GetBoneName(0));
    EXPECT_EQ(1u, pSkeleton->FindBone("Knee"));
    EXPECT_EQ(InvalidIndex, pSkeleton->FindBone("Ankle"));
    EXPECT_EQ(0u, pSkeleton->GetBoneParent(1));
    EXPECT_EQ(InvalidIndex, pSkeleton->GetBoneParent(0));

    // Reference: the same chain as scene-graph nodes, in skeleton space.
    const auto MakeNode = [&](const Canvas::Transform &t, Canvas::XSceneGraphNode *pParent)
    {
        Gem::TGemPtr<Canvas::XSceneGraphNode> pNode;
        EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pNode, "Reference")));
        pNode->SetLocalTranslation(t.Translation);
        pNode->SetLocalRotation(t.Rotation);
        pNode->SetLocalScale(t.Scale);
        if (pParent)
            EXPECT_TRUE(Succeeded(pParent->AddChild(pNode)));
        return pNode;
    };
    auto pRefHip    = MakeNode(bones[0].RestTransform, nullptr);
    auto pRefKnee   = MakeNode(bones[1].RestTransform, pRefHip);
    auto pRefSocket = MakeNode(socket.LocalTransform, pRefKnee);

    EXPECT_TRUE(AlmostEqual(pSkeleton->GetBoneMatrix(0), pRefHip->GetGlobalMatrix()));
    EXPECT_TRUE(AlmostEqual(pSkeleton->GetBoneMatrix(1), pRefKnee->GetGlobalMatrix()));
    EXPECT_TRUE(AlmostEqual(pSkeleton->GetBoneMatrix(2), FloatMatrix4x4::Identity())); // out of range

    // At instantiation the socket is at its bone's rest pose: local * bone * armature.
    EXPECT_TRUE(AlmostEqual(pSocket->GetGlobalMatrix(), pRefSocket->GetGlobalMatrix() * pArmature->GetGlobalMatrix()));

    // Posing a bone moves the socket, and the content under it, on the next update.
    Canvas::Transform bent = bones[1].RestTransform;
    bent.Rotation = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, 1.2f);
    EXPECT_TRUE(Succeeded(pSkeleton->SetBoneLocalTransform(1, &bent)));
    pRefKnee->SetLocalRotation(bent.Rotation);
    EXPECT_TRUE(AlmostEqual(pSkeleton->GetBoneMatrix(1), pRefKnee->GetGlobalMatrix()));

    EXPECT_TRUE(Succeeded(result.pInstanceRoot->Update(0.0f)));
    EXPECT_TRUE(AlmostEqual(pSocket->GetGlobalMatrix(), pRefSocket->GetGlobalMatrix() * pArmature->GetGlobalMatrix()));
    EXPECT_TRUE(AlmostEqual(pProp->GetGlobalMatrix(), pSocket->GetGlobalMatrix()));

    Canvas::Transform read;
    EXPECT_TRUE(Succeeded(pSkeleton->GetBoneLocalTransform(1, &read)));
    EXPECT_TRUE(AlmostEqual(read.Rotation, bent.Rotation));
    EXPECT_EQ(Gem::Result::InvalidArg, pSkeleton->GetBoneLocalTransform(2, &read));
    EXPECT_EQ(Gem::Result::InvalidArg, pSkeleton->SetBoneLocalTransform(2, &bent));

    // Resetting returns every bone, and after an update the socket, to rest.
    pSkeleton->ResetToRestPose();
    pRefKnee->SetLocalRotation(bones[1].RestTransform.Rotation);
    EXPECT_TRUE(Succeeded(result.pInstanceRoot->Update(0.0f)));
    EXPECT_TRUE(AlmostEqual(pSocket->GetGlobalMatrix(), pRefSocket->GetGlobalMatrix() * pArmature->GetGlobalMatrix()));

    // A socket node removed from the skeleton's node keeps its last transform.
    Gem::TGemPtr<Canvas::XSceneGraphNode> pRemoved = pSocket;
    const FloatVector4 lastTranslation = pRemoved->GetLocalTranslation();
    EXPECT_TRUE(Succeeded(pArmature->RemoveChild(pRemoved)));
    EXPECT_TRUE(Succeeded(pSkeleton->SetBoneLocalTransform(1, &bent)));
    EXPECT_TRUE(Succeeded(result.pInstanceRoot->Update(0.0f)));
    EXPECT_TRUE(AlmostEqual(pRemoved->GetLocalTranslation(), lastTranslation));
}

// A skinned mesh instance's palette maps its geometry through each bone's inverse bind pose,
// bone matrix, and the skeleton node's global matrix; the mesh node's own transform plays
// no part.
TEST(CanvasInterfacesTest, SkinBindingComputesPalette)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "Skinned");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t armature = AddNode(pBuilder, "Armature", InvalidIndex, Translation(0.0f, 5.0f, 0.0f));
    const uint32_t body     = AddNode(pBuilder, "Body", armature, Translation(3.0f, 3.0f, 3.0f));

    Canvas::ModelBoneDesc bones[2] = {};
    bones[0].Name          = "Root";
    bones[0].RestTransform = Translation(1.0f, 0.0f, 0.0f);
    bones[1].Name          = "Tip";
    bones[1].ParentIndex   = 0;
    bones[1].RestTransform = Translation(0.0f, 2.0f, 0.0f);
    Canvas::ModelSkeletonDesc skeleton{};
    skeleton.NodeIndex = armature;
    skeleton.BoneCount = 2;
    skeleton.pBones    = bones;
    uint32_t skeletonIndex = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddSkeleton(&skeleton, &skeletonIndex)));

    uint32_t meshIndex = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddMeshData(CreateTriangle(pDevice, "Tri"), &meshIndex)));
    uint32_t skeletonBoneIndices[2] = { 1, 0 };
    FloatMatrix4x4 invBinds[2] = { FloatMatrix4x4::Identity(), FloatMatrix4x4::Identity() };
    invBinds[1][3][0] = 5.0f; // distinguish the second pose
    Canvas::ModelMeshDesc mesh{};
    mesh.NodeIndex            = body;
    mesh.MeshDataIndex        = meshIndex;
    mesh.SkeletonIndex        = skeletonIndex;
    mesh.BoneCount            = 2;
    mesh.pSkeletonBoneIndices = skeletonBoneIndices;
    mesh.pInvBindPoses        = invBinds;
    EXPECT_TRUE(Succeeded(pBuilder->AddMesh(&mesh, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);

    // The builder copied the inverse bind poses.
    invBinds[1][3][0] = 99.0f;

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "TestWorld")));
    Canvas::ModelInstantiateResult result{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));

    Canvas::XSceneGraphNode *pArmature = result.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pArmature, nullptr);
    Canvas::XSceneGraphNode *pBody = pArmature->GetFirstChild();
    ASSERT_NE(pBody, nullptr);
    Canvas::XSkeletonInstance *pSkeleton = FindElement<Canvas::XSkeletonInstance>(pArmature);
    Canvas::XMeshInstance *pMesh = FindElement<Canvas::XMeshInstance>(pBody);
    ASSERT_NE(pSkeleton, nullptr);
    ASSERT_NE(pMesh, nullptr);

    EXPECT_EQ(2u, pMesh->GetSkinBoneCount());
    EXPECT_TRUE(pMesh->GetSkinSkeleton() == pSkeleton);
    EXPECT_EQ(1u, pMesh->GetSkinSkeletonBoneIndex(0));
    EXPECT_EQ(0u, pMesh->GetSkinSkeletonBoneIndex(1));
    EXPECT_EQ(InvalidIndex, pMesh->GetSkinSkeletonBoneIndex(2)); // out of range
    ASSERT_NE(pMesh->GetSkinInvBindPose(1), nullptr);
    EXPECT_FLOAT_EQ(5.0f, (*pMesh->GetSkinInvBindPose(1))[3][0]);
    EXPECT_EQ(nullptr, pMesh->GetSkinInvBindPose(2));

    const auto ExpectPalette = [&]()
    {
        FloatMatrix4x4 palette[2];
        ASSERT_TRUE(Succeeded(pMesh->ComputeSkinMatrices(palette, 2)));
        for (uint32_t b = 0; b < 2; ++b)
        {
            const FloatMatrix4x4 expected = *pMesh->GetSkinInvBindPose(b) *
                pSkeleton->GetBoneMatrix(skeletonBoneIndices[b]) * pArmature->GetGlobalMatrix();
            EXPECT_TRUE(AlmostEqual(palette[b], expected)) << "bone " << b;
        }
    };
    ExpectPalette();

    // The model's bounds place the skinned mesh by its rest palette, not by its node.
    {
        FloatMatrix4x4 palette[2];
        ASSERT_TRUE(Succeeded(pMesh->ComputeSkinMatrices(palette, 2)));
        const AABB local = pMesh->GetMeshData()->GetLocalBounds();
        AABB expected;
        expected.Reset();
        for (const FloatMatrix4x4 &m : palette)
            expected.ExpandToInclude(local.Transform(m));
        const AABB bounds = pModel->GetBounds();
        EXPECT_TRUE(AlmostEqual(bounds.Min, expected.Min));
        EXPECT_TRUE(AlmostEqual(bounds.Max, expected.Max));
    }

    FloatMatrix4x4 one[1];
    EXPECT_EQ(Gem::Result::InvalidArg, pMesh->ComputeSkinMatrices(one, 1));

    // Moving the instance, or posing a bone, moves the palette with it.
    result.pInstanceRoot->SetLocalTranslation(FloatVector4(0.0f, 0.0f, 7.0f, 0.0f));
    Canvas::Transform pose = bones[1].RestTransform;
    pose.Rotation = FloatQuaternion::FromEulerXYZ(0.3f, 0.0f, 0.0f);
    EXPECT_TRUE(Succeeded(pSkeleton->SetBoneLocalTransform(1, &pose)));
    ExpectPalette();

    // A binding to a bone the skeleton lacks is rejected and leaves the binding intact.
    const uint32_t badBone = 2;
    Canvas::SkinBindingDesc bad{};
    bad.pSkeleton            = pSkeleton;
    bad.BoneCount            = 1;
    bad.pSkeletonBoneIndices = &badBone;
    bad.pInvBindPoses        = invBinds;
    EXPECT_TRUE(Gem::Failed(pMesh->SetSkinBinding(&bad)));
    EXPECT_EQ(2u, pMesh->GetSkinBoneCount());

    // A null descriptor clears the binding.
    EXPECT_TRUE(Succeeded(pMesh->SetSkinBinding(nullptr)));
    EXPECT_EQ(0u, pMesh->GetSkinBoneCount());
    EXPECT_EQ(nullptr, pMesh->GetSkinSkeleton());
    EXPECT_TRUE(Succeeded(pMesh->ComputeSkinMatrices(nullptr, 0)));
}

// Exercises the per-instance controller's playback, clip-time looping at the Duration
// boundary, and bind-pose restoration.  Also confirms the controller keeps the model's
// shared clip data alive after the caller releases its own model reference.
TEST(CanvasInterfacesTest, AnimationControllerPlaybackLoopAndReset)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    // Model with a single node authored at a distinctive bind pose.
    auto pBuilder = BeginModel(pCanvas, pDevice, "AnimModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t node = AddNode(pBuilder, "Bone", InvalidIndex, Translation(7.0f, 7.0f, 7.0f));

    // One clip translating X from 0 to 40 over 2 seconds.
    const uint32_t target = AddNodeTarget(pBuilder, node, Canvas::AnimationTargetType::SceneNodeTranslation);
    const float times[2] = { 0.0f, 2.0f };
    const float values[6] = { 0.0f, 0.0f, 0.0f,  40.0f, 0.0f, 0.0f };
    const Canvas::AnimationTrackDesc track = Track(Canvas::AnimationValueType::Float3, times, values, 2);
    const Canvas::AnimationClipDesc clip = Clip("Slide", 2.0f, &track, &target, 1);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&clip, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);
    EXPECT_EQ(1u, pModel->GetAnimationClipCount());
    EXPECT_STREQ("Slide", pModel->GetAnimationClipName(0));

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "AnimWorld")));

    Canvas::ModelInstantiateResult result{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));
    ASSERT_NE(result.pAnimationController, nullptr);
    ASSERT_NE(result.pInstanceRoot, nullptr);

    Canvas::XAnimationController* pCtrl = result.pAnimationController;
    Canvas::XSceneGraphNode*      pNode = result.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pNode, nullptr);

    // Releasing the caller's model reference must not invalidate the controller's clips:
    // the controller holds its own strong reference to the model.
    pModel = nullptr;

    EXPECT_EQ(1u, pCtrl->GetClipCount());
    EXPECT_EQ(0u, pCtrl->FindClip("Slide"));
    EXPECT_STREQ("Slide", pCtrl->GetClipName(0));
    EXPECT_EQ(Canvas::InvalidClipIndex, pCtrl->FindClip("Missing"));
    EXPECT_FALSE(pCtrl->IsPlaying());

    // Play and advance to the midpoint: X interpolates to 20.
    EXPECT_TRUE(Succeeded(pCtrl->Play(0, 0.0f)));
    EXPECT_TRUE(pCtrl->IsPlaying());
    EXPECT_TRUE(Succeeded(pCtrl->Update(1.0f)));
    EXPECT_FLOAT_EQ(1.0f, pCtrl->GetTime());
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(),
                            FloatVector4(20.0f, 0.0f, 0.0f, 1.0f)));

    // Advancing past Duration wraps the clip time back into [0, Duration).
    EXPECT_TRUE(Succeeded(pCtrl->Update(1.5f)));   // 1.0 + 1.5 = 2.5 -> 0.5
    EXPECT_FLOAT_EQ(0.5f, pCtrl->GetTime());
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(),
                            FloatVector4(10.0f, 0.0f, 0.0f, 1.0f)));

    // ResetToBindPose restores the instantiation-time TRS and stops playback.
    pCtrl->ResetToBindPose();
    EXPECT_FALSE(pCtrl->IsPlaying());
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(),
                            FloatVector4(7.0f, 7.0f, 7.0f, 1.0f)));
}

// XWorld::Update updates every world object, including a detached one whose nodes are outside
// the scene graph: its animation controller advances like one in the scene graph.
TEST(CanvasInterfacesTest, WorldUpdateAdvancesDetachedWorldObjects)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "SlideModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t node = AddNode(pBuilder, "Body", InvalidIndex, Translation(0.0f, 0.0f, 0.0f));
    const uint32_t target = AddNodeTarget(pBuilder, node, Canvas::AnimationTargetType::SceneNodeTranslation);
    const float times[2] = { 0.0f, 2.0f };
    const float values[6] = { 0.0f, 0.0f, 0.0f,  40.0f, 0.0f, 0.0f };
    const Canvas::AnimationTrackDesc track = Track(Canvas::AnimationValueType::Float3, times, values, 2);
    const Canvas::AnimationClipDesc clip = Clip("Slide", 2.0f, &track, &target, 1);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&clip, nullptr)));
    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "PoolWorld")));

    // One instance in the scene graph, one under a node outside it
    Gem::TGemPtr<Canvas::XSceneGraphNode> pShownNode, pPooledNode;
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pShownNode, "Shown")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pPooledNode, "Pooled")));
    ASSERT_TRUE(Succeeded(pWorld->GetRootNode()->AddChild(pShownNode)));

    Canvas::ModelInstantiateResult shown{}, pooled{};
    ASSERT_TRUE(Succeeded(pModel->Instantiate(pShownNode, &shown)));
    ASSERT_TRUE(Succeeded(pModel->Instantiate(pPooledNode, &pooled)));
    ASSERT_NE(shown.pAnimationController, nullptr);
    ASSERT_NE(pooled.pAnimationController, nullptr);

    Gem::TGemPtr<Canvas::XWorldObject> pShown, pPooled;
    ASSERT_TRUE(Succeeded(pWorld->CreateWorldObject(pShownNode, &pShown, "Shown")));
    ASSERT_TRUE(Succeeded(pWorld->CreateWorldObject(pPooledNode, &pPooled, "Pooled")));

    ASSERT_TRUE(Succeeded(shown.pAnimationController->Play(0, 0.0f)));
    ASSERT_TRUE(Succeeded(pooled.pAnimationController->Play(0, 0.0f)));
    ASSERT_TRUE(Succeeded(pWorld->Update(1.0f)));

    EXPECT_FLOAT_EQ(1.0f, shown.pAnimationController->GetTime());
    EXPECT_FLOAT_EQ(1.0f, pooled.pAnimationController->GetTime());
}

// XWorld::Update updates the active world objects and leaves the inactive ones as they are.
// Deactivating and activating in mixed order exercises the swaps across the active boundary.
TEST(CanvasInterfacesTest, WorldUpdateSkipsInactiveWorldObjects)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "SlideModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t node = AddNode(pBuilder, "Body", InvalidIndex, Translation(0.0f, 0.0f, 0.0f));
    const uint32_t target = AddNodeTarget(pBuilder, node, Canvas::AnimationTargetType::SceneNodeTranslation);
    const float times[2] = { 0.0f, 8.0f };
    const float values[6] = { 0.0f, 0.0f, 0.0f,  80.0f, 0.0f, 0.0f };
    const Canvas::AnimationTrackDesc track = Track(Canvas::AnimationValueType::Float3, times, values, 2);
    const Canvas::AnimationClipDesc clip = Clip("Slide", 8.0f, &track, &target, 1);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&clip, nullptr)));
    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);

    Gem::TGemPtr<Canvas::XWorld> pWorld, pOtherWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "PoolWorld")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pOtherWorld, "OtherWorld")));

    // Three pooled projectiles, each a detached world object playing the same clip
    constexpr int kCount = 3;
    Gem::TGemPtr<Canvas::XSceneGraphNode> pNodes[kCount];
    Gem::TGemPtr<Canvas::XWorldObject> pObjects[kCount];
    Canvas::XAnimationController *pControllers[kCount] = {};
    for (int i = 0; i < kCount; ++i)
    {
        ASSERT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[i], "Projectile")));
        Canvas::ModelInstantiateResult instance{};
        ASSERT_TRUE(Succeeded(pModel->Instantiate(pNodes[i], &instance)));
        ASSERT_NE(instance.pAnimationController, nullptr);
        pControllers[i] = instance.pAnimationController;
        ASSERT_TRUE(Succeeded(pControllers[i]->Play(0, 0.0f)));
        ASSERT_TRUE(Succeeded(pWorld->CreateWorldObject(pNodes[i], &pObjects[i], "Projectile")));
    }

    auto expectTimes = [&](float t0, float t1, float t2)
    {
        EXPECT_FLOAT_EQ(t0, pControllers[0]->GetTime());
        EXPECT_FLOAT_EQ(t1, pControllers[1]->GetTime());
        EXPECT_FLOAT_EQ(t2, pControllers[2]->GetTime());
    };

    // All start active
    ASSERT_TRUE(Succeeded(pWorld->Update(1.0f)));
    expectTimes(1.0f, 1.0f, 1.0f);

    // Deactivate the first; deactivating it again changes nothing
    ASSERT_TRUE(Succeeded(pWorld->DeactivateObject(pObjects[0])));
    ASSERT_TRUE(Succeeded(pWorld->DeactivateObject(pObjects[0])));
    ASSERT_TRUE(Succeeded(pWorld->Update(1.0f)));
    expectTimes(1.0f, 2.0f, 2.0f);

    // Deactivate the last, then reactivate the first
    ASSERT_TRUE(Succeeded(pWorld->DeactivateObject(pObjects[2])));
    ASSERT_TRUE(Succeeded(pWorld->ActivateObject(pObjects[0])));
    ASSERT_TRUE(Succeeded(pWorld->ActivateObject(pObjects[0])));
    ASSERT_TRUE(Succeeded(pWorld->Update(1.0f)));
    expectTimes(2.0f, 3.0f, 2.0f);

    // Deactivate all, then reactivate the middle one
    for (int i = 0; i < kCount; ++i)
        ASSERT_TRUE(Succeeded(pWorld->DeactivateObject(pObjects[i])));
    ASSERT_TRUE(Succeeded(pWorld->Update(1.0f)));
    expectTimes(2.0f, 3.0f, 2.0f);
    ASSERT_TRUE(Succeeded(pWorld->ActivateObject(pObjects[1])));
    ASSERT_TRUE(Succeeded(pWorld->Update(1.0f)));
    expectTimes(2.0f, 4.0f, 2.0f);

    // Only the world's own world objects
    EXPECT_EQ(Gem::Result::NotFound, pOtherWorld->DeactivateObject(pObjects[1]));
    EXPECT_EQ(Gem::Result::NotFound, pOtherWorld->ActivateObject(pObjects[0]));
    EXPECT_EQ(Gem::Result::NotFound, pWorld->ActivateObject(nullptr));
}

// Exercises cross-fade blending between two clips: the pose interpolates from the
// pre-blend snapshot toward the new clip, and snaps fully to the new clip once the
// blend duration elapses.
TEST(CanvasInterfacesTest, AnimationControllerCrossFadeBlend)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "BlendModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t node = AddNode(pBuilder, "Bone", InvalidIndex);

    // Two single-key clips that hold the node at constant, distinct translations.
    const uint32_t target = AddNodeTarget(pBuilder, node, Canvas::AnimationTargetType::SceneNodeTranslation);
    const float keyTime = 0.0f;
    const float keyA[3] = { 0.0f, 0.0f, 0.0f };
    const Canvas::AnimationTrackDesc trackA = Track(Canvas::AnimationValueType::Float3, &keyTime, keyA, 1);
    const Canvas::AnimationClipDesc clipA = Clip("A", 10.0f, &trackA, &target, 1);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&clipA, nullptr)));

    const float keyB[3] = { 100.0f, 0.0f, 0.0f };
    const Canvas::AnimationTrackDesc trackB = Track(Canvas::AnimationValueType::Float3, &keyTime, keyB, 1);
    const Canvas::AnimationClipDesc clipB = Clip("B", 10.0f, &trackB, &target, 1);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&clipB, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "BlendWorld")));

    Canvas::ModelInstantiateResult result{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));
    ASSERT_NE(result.pAnimationController, nullptr);

    Canvas::XAnimationController* pCtrl = result.pAnimationController;
    Canvas::XSceneGraphNode*      pNode = result.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pNode, nullptr);

    // Settle on clip A (X = 0).
    EXPECT_TRUE(Succeeded(pCtrl->Play(0, 0.0f)));
    EXPECT_TRUE(Succeeded(pCtrl->Update(0.1f)));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(),
                            FloatVector4(0.0f, 0.0f, 0.0f, 1.0f)));

    // Cross-fade to clip B (X = 100) over 1 second; halfway through, X is the 50/50 blend.
    EXPECT_TRUE(Succeeded(pCtrl->PlayByName("B", 1.0f)));
    EXPECT_EQ(1u, pCtrl->GetActiveClipIndex());
    EXPECT_TRUE(Succeeded(pCtrl->Update(0.5f)));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(),
                            FloatVector4(50.0f, 0.0f, 0.0f, 1.0f)));

    // Once the blend duration elapses the pose snaps fully to clip B.
    EXPECT_TRUE(Succeeded(pCtrl->Update(0.6f)));   // blend time 1.1 >= 1.0
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(),
                            FloatVector4(100.0f, 0.0f, 0.0f, 1.0f)));

    // Subsequent updates keep applying clip B directly (blend state cleared).
    EXPECT_TRUE(Succeeded(pCtrl->Update(0.1f)));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(),
                            FloatVector4(100.0f, 0.0f, 0.0f, 1.0f)));
}

// A bone track drives the bone in each instance's skeleton instance.  Ticked through the
// instance root, the controller runs before the skeleton node updates, so the socket
// follows the animated bone in the same update.
TEST(CanvasInterfacesTest, AnimationControllerDrivesBones)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "ArmModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t armature = AddNode(pBuilder, "Armature", InvalidIndex);

    Canvas::ModelBoneDesc bone{};
    bone.Name          = "Arm";
    bone.RestTransform = Translation(0.0f, 0.0f, 1.0f);
    Canvas::ModelSkeletonDesc skeleton{};
    skeleton.NodeIndex = armature;
    skeleton.BoneCount = 1;
    skeleton.pBones    = &bone;
    uint32_t skeletonIndex = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddSkeleton(&skeleton, &skeletonIndex)));

    Canvas::ModelSocketDesc socket{};
    socket.Name          = "Hand";
    socket.SkeletonIndex = skeletonIndex;
    socket.BoneIndex     = 0;
    EXPECT_TRUE(Succeeded(pBuilder->AddSocket(&socket, nullptr)));

    // X from 0 to 4 over 2 seconds; Z held at the rest height.
    Canvas::AnimationTargetDesc targetDesc{};
    targetDesc.Type          = Canvas::AnimationTargetType::BoneTranslation;
    targetDesc.SkeletonIndex = skeletonIndex;
    targetDesc.Index         = 0;
    uint32_t target = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationTarget(&targetDesc, &target)));

    const float times[2] = { 0.0f, 2.0f };
    const float values[6] = { 0.0f, 0.0f, 1.0f,  4.0f, 0.0f, 1.0f };
    const Canvas::AnimationTrackDesc track = Track(Canvas::AnimationValueType::Float3, times, values, 2);
    const Canvas::AnimationClipDesc clip = Clip("Swing", 2.0f, &track, &target, 1);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&clip, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "ArmWorld")));
    Canvas::ModelInstantiateResult result{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));
    ASSERT_NE(result.pAnimationController, nullptr);

    Canvas::XSceneGraphNode *pArmature = result.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pArmature, nullptr);
    Canvas::XSceneGraphNode *pHand = pArmature->GetFirstChild();
    ASSERT_NE(pHand, nullptr);
    Canvas::XSkeletonInstance *pSkeleton = FindElement<Canvas::XSkeletonInstance>(pArmature);
    ASSERT_NE(pSkeleton, nullptr);

    Canvas::XAnimationController *pCtrl = result.pAnimationController;
    EXPECT_TRUE(pCtrl->ValidateForNode(result.pInstanceRoot));
    EXPECT_FALSE(pCtrl->ValidateForNode(pHand));   // the skeleton's node is not under the socket

    EXPECT_TRUE(Succeeded(pCtrl->Play(0, 0.0f)));
    EXPECT_TRUE(Succeeded(result.pInstanceRoot->Update(1.0f)));

    Canvas::Transform pose;
    EXPECT_TRUE(Succeeded(pSkeleton->GetBoneLocalTransform(0, &pose)));
    EXPECT_TRUE(AlmostEqual(pose.Translation, FloatVector4(2.0f, 0.0f, 1.0f, 1.0f)));
    EXPECT_TRUE(AlmostEqual(pHand->GetLocalTranslation(), FloatVector4(2.0f, 0.0f, 1.0f, 1.0f)));

    // ResetToBindPose returns the bone to rest; the socket follows on the next update.
    pCtrl->ResetToBindPose();
    EXPECT_TRUE(Succeeded(pSkeleton->GetBoneLocalTransform(0, &pose)));
    EXPECT_TRUE(AlmostEqual(pose.Translation, FloatVector4(0.0f, 0.0f, 1.0f, 1.0f)));
    EXPECT_TRUE(Succeeded(result.pInstanceRoot->Update(0.0f)));
    EXPECT_TRUE(AlmostEqual(pHand->GetLocalTranslation(), FloatVector4(0.0f, 0.0f, 1.0f, 1.0f)));
}

// Each clip drives only the targets it has tracks for; every other target takes its value at
// instantiation, including after switching from a clip that drove it.
TEST(CanvasInterfacesTest, AnimationControllerFillsUntrackedTargets)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "PropertyModel");
    ASSERT_NE(pBuilder.Get(), nullptr);

    // Bind pose: translated, turned 90 degrees about Z, and scaled by 2.
    const float s45 = std::sqrt(0.5f);
    Canvas::Transform bind;
    bind.Translation = FloatVector4(7.0f, 7.0f, 7.0f, 1.0f);
    bind.Rotation    = FloatQuaternion(0.0f, 0.0f, s45, s45);
    bind.Scale       = FloatVector4(2.0f, 2.0f, 2.0f, 0.0f);
    const uint32_t node = AddNode(pBuilder, "Node", InvalidIndex, bind);

    // "Spin" turns from identity to 90 degrees about X over 2 seconds; "Slide" moves along X.
    const uint32_t rotationTarget    = AddNodeTarget(pBuilder, node, Canvas::AnimationTargetType::SceneNodeRotation);
    const uint32_t translationTarget = AddNodeTarget(pBuilder, node, Canvas::AnimationTargetType::SceneNodeTranslation);
    const float times[2] = { 0.0f, 2.0f };
    const float spin[8]  = { 0.0f, 0.0f, 0.0f, 1.0f,  s45, 0.0f, 0.0f, s45 };
    const float slide[6] = { 0.0f, 0.0f, 0.0f,  10.0f, 0.0f, 0.0f };
    const Canvas::AnimationTrackDesc spinTrack  = Track(Canvas::AnimationValueType::Quaternion, times, spin, 2);
    const Canvas::AnimationTrackDesc slideTrack = Track(Canvas::AnimationValueType::Float3, times, slide, 2);
    const Canvas::AnimationClipDesc spinClip  = Clip("Spin", 4.0f, &spinTrack, &rotationTarget, 1);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&spinClip, nullptr)));
    const Canvas::AnimationClipDesc slideClip = Clip("Slide", 4.0f, &slideTrack, &translationTarget, 1);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&slideClip, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);
    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "PropertyWorld")));
    Canvas::ModelInstantiateResult result{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));
    ASSERT_NE(result.pAnimationController, nullptr);
    Canvas::XAnimationController *pCtrl = result.pAnimationController;
    Canvas::XSceneGraphNode *pNode = result.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pNode, nullptr);

    // Halfway through Spin: 45 degrees about X, with translation and scale at the bind pose.
    const float s225 = std::sin(0.3926991f), c225 = std::cos(0.3926991f);
    EXPECT_TRUE(Succeeded(pCtrl->PlayByName("Spin", 0.0f)));
    EXPECT_TRUE(Succeeded(pCtrl->Update(1.0f)));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalRotation(), FloatQuaternion(s225, 0.0f, 0.0f, c225)));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(), bind.Translation));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalScale(), bind.Scale));

    // Slide moves the node and returns its rotation to the bind pose.
    EXPECT_TRUE(Succeeded(pCtrl->PlayByName("Slide", 0.0f)));
    EXPECT_TRUE(Succeeded(pCtrl->Update(1.0f)));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(), FloatVector4(5.0f, 0.0f, 0.0f, 1.0f)));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalRotation(), bind.Rotation));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalScale(), bind.Scale));
}

// A step track holds each key's value until the next key.
TEST(CanvasInterfacesTest, AnimationControllerStepInterpolation)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "StepModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t node = AddNode(pBuilder, "Node", InvalidIndex);

    const uint32_t target = AddNodeTarget(pBuilder, node, Canvas::AnimationTargetType::SceneNodeTranslation);
    const float times[3] = { 0.0f, 1.0f, 2.0f };
    const float values[9] = { 0.0f, 0.0f, 0.0f,  10.0f, 0.0f, 0.0f,  20.0f, 0.0f, 0.0f };
    const Canvas::AnimationTrackDesc track =
        Track(Canvas::AnimationValueType::Float3, times, values, 3, Canvas::AnimationInterpolation::Step);
    const Canvas::AnimationClipDesc clip = Clip("Step", 3.0f, &track, &target, 1);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&clip, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);
    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "StepWorld")));
    Canvas::ModelInstantiateResult result{};
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));
    ASSERT_NE(result.pAnimationController, nullptr);
    Canvas::XAnimationController *pCtrl = result.pAnimationController;
    Canvas::XSceneGraphNode *pNode = result.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pNode, nullptr);

    EXPECT_TRUE(Succeeded(pCtrl->Play(0, 0.0f)));
    EXPECT_TRUE(Succeeded(pCtrl->Update(0.5f)));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(), FloatVector4(0.0f, 0.0f, 0.0f, 1.0f)));
    EXPECT_TRUE(Succeeded(pCtrl->Update(1.0f)));     // t = 1.5
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(), FloatVector4(10.0f, 0.0f, 0.0f, 1.0f)));
    EXPECT_TRUE(Succeeded(pCtrl->Update(1.0f)));     // t = 2.5, past the last key
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(), FloatVector4(20.0f, 0.0f, 0.0f, 1.0f)));
}

// Instantiates pModel in pWorld and returns the instance's animation controller.
Canvas::XAnimationController *InstantiateAnimated(Canvas::XModel *pModel, Canvas::XWorld *pWorld,
    Canvas::ModelInstantiateResult &result)
{
    EXPECT_TRUE(Succeeded(pModel->Instantiate(pWorld->GetRootNode(), &result)));
    return result.pAnimationController;
}

// The value of target at clip time t: count components, with the rest zero.
Canvas::Math::FloatVector4 ValueAt(Canvas::XAnimationController *pCtrl, uint32_t target, float t, uint32_t count = 1)
{
    pCtrl->SetTime(t);
    EXPECT_TRUE(Succeeded(pCtrl->Update(0.0f)));
    float value[4] = {};
    EXPECT_TRUE(Succeeded(pCtrl->GetTargetValue(target, value, count)));
    return Canvas::Math::FloatVector4(value[0], value[1], value[2], value[3]);
}

// A cubic segment's value is Value[i] + C1 u + C2 u^2 + C3 u^3, with u running from 0 to 1 over
// the segment whatever its duration.
TEST(CanvasInterfacesTest, AnimationControllerCubicInterpolation)
{
    using namespace Canvas::Math;
    using Canvas::AnimationValueType;
    using Canvas::AnimationInterpolation;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "CubicModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t node = AddNode(pBuilder, "Node", InvalidIndex);

    const uint32_t curveTarget  = AddValueTarget(pBuilder, AnimationValueType::Float);
    const uint32_t cubicTarget  = AddValueTarget(pBuilder, AnimationValueType::Float);
    const uint32_t linearTarget = AddValueTarget(pBuilder, AnimationValueType::Float);
    const uint32_t packedTarget = AddValueTarget(pBuilder, AnimationValueType::Float3);
    const uint32_t jumpTarget   = AddValueTarget(pBuilder, AnimationValueType::Float);
    const uint32_t nodeTarget   = AddNodeTarget(pBuilder, node, Canvas::AnimationTargetType::SceneNodeTranslation);

    // Segment 0 (1 s) eases from 0 to 10: the Hermite curve with zero tangents.  Segment 1
    // (2 s) runs from 10 to 4 along an arbitrary cubic whose coefficients sum to -6.
    const float curveTimes[3]  = { 0.0f, 1.0f, 3.0f };
    const float curveValues[3] = { 0.0f, 10.0f, 4.0f };
    const float curveCoefficients[6] = { 0.0f, 30.0f, -20.0f,  2.0f, -3.0f, -5.0f };
    Canvas::AnimationTrackDesc curve = Track(AnimationValueType::Float, curveTimes, curveValues, 3, AnimationInterpolation::Cubic);
    curve.pCoefficients = curveCoefficients;

    // A cubic with only C1 is linear.
    const float lineTimes[2]  = { 0.0f, 4.0f };
    const float lineValues[2] = { 2.0f, 10.0f };
    const float lineCoefficients[3] = { 8.0f, 0.0f, 0.0f };
    Canvas::AnimationTrackDesc cubicLine = Track(AnimationValueType::Float, lineTimes, lineValues, 2, AnimationInterpolation::Cubic);
    cubicLine.pCoefficients = lineCoefficients;
    const Canvas::AnimationTrackDesc linearLine = Track(AnimationValueType::Float, lineTimes, lineValues, 2);

    // Each component has its own coefficients: C1, C2, and C3 are each three floats.
    const float packedTimes[2]  = { 0.0f, 2.0f };
    const float packedValues[6] = { 1.0f, 2.0f, 3.0f,  2.0f, 3.0f, 4.0f };
    const float packedCoefficients[9] = { 1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f };
    Canvas::AnimationTrackDesc packed = Track(AnimationValueType::Float3, packedTimes, packedValues, 2, AnimationInterpolation::Cubic);
    packed.pCoefficients = packedCoefficients;

    // Two keys at t = 1 make the value jump from 10 to 20.
    const float jumpTimes[4]  = { 0.0f, 1.0f, 1.0f, 2.0f };
    const float jumpValues[4] = { 0.0f, 10.0f, 20.0f, 30.0f };
    const float jumpCoefficients[9] = { 10.0f, 0.0f, 0.0f,  0.0f, 0.0f, 0.0f,  10.0f, 0.0f, 0.0f };
    Canvas::AnimationTrackDesc jump = Track(AnimationValueType::Float, jumpTimes, jumpValues, 4, AnimationInterpolation::Cubic);
    jump.pCoefficients = jumpCoefficients;

    // A node's translation eases from the origin to X = 10 over 2 seconds.
    const float nodeTimes[2]  = { 0.0f, 2.0f };
    const float nodeValues[6] = { 0.0f, 0.0f, 0.0f,  10.0f, 0.0f, 0.0f };
    const float nodeCoefficients[9] = { 0.0f, 0.0f, 0.0f,  30.0f, 0.0f, 0.0f,  -20.0f, 0.0f, 0.0f };
    Canvas::AnimationTrackDesc nodeTrack = Track(AnimationValueType::Float3, nodeTimes, nodeValues, 2, AnimationInterpolation::Cubic);
    nodeTrack.pCoefficients = nodeCoefficients;

    const Canvas::AnimationTrackDesc tracks[6] = { curve, cubicLine, linearLine, packed, jump, nodeTrack };
    const uint32_t targets[6] = { curveTarget, cubicTarget, linearTarget, packedTarget, jumpTarget, nodeTarget };
    const Canvas::AnimationClipDesc clip = Clip("Curves", 5.0f, tracks, targets, 6);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&clip, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);
    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "CubicWorld")));
    Canvas::ModelInstantiateResult result{};
    Canvas::XAnimationController *pCtrl = InstantiateAnimated(pModel.Get(), pWorld.Get(), result);
    ASSERT_NE(pCtrl, nullptr);
    Canvas::XSceneGraphNode *pNode = result.pInstanceRoot->GetFirstChild();
    ASSERT_NE(pNode, nullptr);
    EXPECT_TRUE(Succeeded(pCtrl->Play(0, 0.0f)));

    // Every key's value, and the closed form between them.
    EXPECT_FLOAT_EQ(0.0f,      ValueAt(pCtrl, curveTarget, 0.0f).X);
    EXPECT_FLOAT_EQ(1.5625f,   ValueAt(pCtrl, curveTarget, 0.25f).X);    // 30/16 - 20/64
    EXPECT_FLOAT_EQ(5.0f,      ValueAt(pCtrl, curveTarget, 0.5f).X);
    EXPECT_FLOAT_EQ(10.0f,     ValueAt(pCtrl, curveTarget, 1.0f).X);
    EXPECT_FLOAT_EQ(9.625f,    ValueAt(pCtrl, curveTarget, 2.0f).X);     // u = 0.5 in a 2 s segment
    EXPECT_FLOAT_EQ(7.703125f, ValueAt(pCtrl, curveTarget, 2.5f).X);     // u = 0.75
    EXPECT_FLOAT_EQ(4.0f,      ValueAt(pCtrl, curveTarget, 3.0f).X);
    EXPECT_FLOAT_EQ(4.0f,      ValueAt(pCtrl, curveTarget, 4.0f).X);     // held after the last key

    for (float t : { 0.0f, 0.7f, 1.9f, 3.3f, 4.5f })
        EXPECT_FLOAT_EQ(ValueAt(pCtrl, linearTarget, t).X, ValueAt(pCtrl, cubicTarget, t).X) << "t = " << t;

    EXPECT_TRUE(AlmostEqual(ValueAt(pCtrl, packedTarget, 1.0f, 3), FloatVector4(1.5f, 2.25f, 3.125f, 0.0f)));

    EXPECT_FLOAT_EQ(5.0f,  ValueAt(pCtrl, jumpTarget, 0.5f).X);
    EXPECT_FLOAT_EQ(20.0f, ValueAt(pCtrl, jumpTarget, 1.0f).X);
    EXPECT_FLOAT_EQ(25.0f, ValueAt(pCtrl, jumpTarget, 1.5f).X);

    EXPECT_TRUE(AlmostEqual(ValueAt(pCtrl, nodeTarget, 1.0f, 3), FloatVector4(5.0f, 0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(AlmostEqual(pNode->GetLocalTranslation(), FloatVector4(5.0f, 0.0f, 0.0f, 1.0f)));
}

// Two keys at the same time change the value instantly, to the later key's value, wherever
// the shared time falls in the track: at the first key, between keys, or at the last key.
TEST(CanvasInterfacesTest, AnimationControllerSharedKeyTimes)
{
    using Canvas::AnimationValueType;
    using Canvas::AnimationInterpolation;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "SharedKeyModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    AddNode(pBuilder, "Node", InvalidIndex);

    const uint32_t firstTarget = AddValueTarget(pBuilder, AnimationValueType::Float);
    const uint32_t lastTarget  = AddValueTarget(pBuilder, AnimationValueType::Float);
    const uint32_t stepTarget  = AddValueTarget(pBuilder, AnimationValueType::Float);

    const float firstTimes[3]  = { 1.0f, 1.0f, 2.0f };
    const float firstValues[3] = { 10.0f, 20.0f, 30.0f };
    const float lastTimes[3]   = { 0.0f, 2.0f, 2.0f };
    const float lastValues[3]  = { 0.0f, 10.0f, 40.0f };
    const float stepTimes[3]   = { 0.0f, 1.0f, 1.0f };
    const float stepValues[3]  = { 5.0f, 6.0f, 7.0f };
    const Canvas::AnimationTrackDesc tracks[3] =
    {
        Track(AnimationValueType::Float, firstTimes, firstValues, 3),
        Track(AnimationValueType::Float, lastTimes, lastValues, 3),
        Track(AnimationValueType::Float, stepTimes, stepValues, 3, AnimationInterpolation::Step),
    };
    const uint32_t targets[3] = { firstTarget, lastTarget, stepTarget };
    const Canvas::AnimationClipDesc clip = Clip("Shared", 3.0f, tracks, targets, 3);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&clip, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);
    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "SharedKeyWorld")));
    Canvas::ModelInstantiateResult result{};
    Canvas::XAnimationController *pCtrl = InstantiateAnimated(pModel.Get(), pWorld.Get(), result);
    ASSERT_NE(pCtrl, nullptr);
    EXPECT_TRUE(Succeeded(pCtrl->Play(0, 0.0f)));

    EXPECT_FLOAT_EQ(10.0f, ValueAt(pCtrl, firstTarget, 0.5f).X);
    EXPECT_FLOAT_EQ(20.0f, ValueAt(pCtrl, firstTarget, 1.0f).X);
    EXPECT_FLOAT_EQ(25.0f, ValueAt(pCtrl, firstTarget, 1.5f).X);

    EXPECT_FLOAT_EQ(5.0f,  ValueAt(pCtrl, lastTarget, 1.0f).X);
    EXPECT_FLOAT_EQ(40.0f, ValueAt(pCtrl, lastTarget, 2.0f).X);
    EXPECT_FLOAT_EQ(40.0f, ValueAt(pCtrl, lastTarget, 2.5f).X);

    EXPECT_FLOAT_EQ(5.0f, ValueAt(pCtrl, stepTarget, 0.5f).X);
    EXPECT_FLOAT_EQ(7.0f, ValueAt(pCtrl, stepTarget, 1.0f).X);
}

// A Value target is zero while no clip drives it.  The application reads it from the
// controller, as many floats as its value type has, including mid-blend.
TEST(CanvasInterfacesTest, AnimationControllerValueTargets)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "WindModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    AddNode(pBuilder, "Node", InvalidIndex);

    const uint32_t wind = AddValueTarget(pBuilder, Canvas::AnimationValueType::Float2);

    const float times[2]  = { 0.0f, 2.0f };
    const float values[4] = { 0.0f, 0.0f,  2.0f, 4.0f };
    const Canvas::AnimationTrackDesc track = Track(Canvas::AnimationValueType::Float2, times, values, 2);
    const Canvas::AnimationClipDesc gust = Clip("Gust", 4.0f, &track, &wind, 1);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&gust, nullptr)));
    const Canvas::AnimationClipDesc still = Clip("Still", 4.0f, nullptr, nullptr, 0);
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&still, nullptr)));

    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);
    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "WindWorld")));
    Canvas::ModelInstantiateResult result{};
    Canvas::XAnimationController *pCtrl = InstantiateAnimated(pModel.Get(), pWorld.Get(), result);
    ASSERT_NE(pCtrl, nullptr);

    // GetTargetValue writes exactly the target's components and nothing past them.
    EXPECT_EQ(1u, pCtrl->GetTargetCount());
    float value[3] = { -1.0f, -1.0f, -1.0f };
    EXPECT_TRUE(Succeeded(pCtrl->GetTargetValue(wind, value, 2)));
    EXPECT_FLOAT_EQ(0.0f, value[0]);
    EXPECT_FLOAT_EQ(0.0f, value[1]);
    EXPECT_FLOAT_EQ(-1.0f, value[2]);
    EXPECT_EQ(Gem::Result::InvalidArg, pCtrl->GetTargetValue(wind, value, 3));
    EXPECT_EQ(Gem::Result::OutOfRange, pCtrl->GetTargetValue(1, value, 2));
    EXPECT_EQ(Gem::Result::BadPointer, pCtrl->GetTargetValue(wind, nullptr, 2));

    EXPECT_TRUE(Succeeded(pCtrl->PlayByName("Gust", 0.0f)));
    EXPECT_TRUE(Succeeded(pCtrl->Update(1.0f)));
    EXPECT_TRUE(Succeeded(pCtrl->GetTargetValue(wind, value, 2)));
    EXPECT_FLOAT_EQ(1.0f, value[0]);
    EXPECT_FLOAT_EQ(2.0f, value[1]);

    // Halfway through a cross-fade to a clip that does not drive it, the value is halfway back.
    EXPECT_TRUE(Succeeded(pCtrl->PlayByName("Still", 1.0f)));
    EXPECT_TRUE(Succeeded(pCtrl->Update(0.5f)));
    EXPECT_TRUE(Succeeded(pCtrl->GetTargetValue(wind, value, 2)));
    EXPECT_FLOAT_EQ(0.5f, value[0]);
    EXPECT_FLOAT_EQ(1.0f, value[1]);
    EXPECT_TRUE(Succeeded(pCtrl->Update(0.6f)));
    EXPECT_TRUE(Succeeded(pCtrl->GetTargetValue(wind, value, 2)));
    EXPECT_FLOAT_EQ(0.0f, value[0]);
    EXPECT_FLOAT_EQ(0.0f, value[1]);

    EXPECT_TRUE(Succeeded(pCtrl->PlayByName("Gust", 0.0f)));
    EXPECT_TRUE(Succeeded(pCtrl->Update(2.0f)));
    pCtrl->ResetToBindPose();
    EXPECT_TRUE(Succeeded(pCtrl->GetTargetValue(wind, value, 2)));
    EXPECT_FLOAT_EQ(0.0f, value[0]);
    EXPECT_FLOAT_EQ(0.0f, value[1]);
}

// AddAnimationTarget and AddAnimationClip validate their descriptors; a rejected call adds nothing.
TEST(CanvasInterfacesTest, ModelBuilderValidatesAnimation)
{
    using Canvas::AnimationValueType;
    using Canvas::AnimationInterpolation;
    using Canvas::AnimationTargetType;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "ValidationModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    const uint32_t node = AddNode(pBuilder, "Node", InvalidIndex);

    // A target's type is known, and a Value target's value type.
    Canvas::AnimationTargetDesc target{};
    target.Type  = AnimationTargetType::SceneNodeTranslation;
    target.Index = node;
    EXPECT_EQ(Gem::Result::BadPointer, pBuilder->AddAnimationTarget(nullptr, nullptr));
    target.Type = static_cast<AnimationTargetType>(7);
    EXPECT_EQ(Gem::Result::InvalidArg, pBuilder->AddAnimationTarget(&target, nullptr));
    target.Type = AnimationTargetType::Value;
    target.ValueType = static_cast<AnimationValueType>(5);
    EXPECT_EQ(Gem::Result::InvalidArg, pBuilder->AddAnimationTarget(&target, nullptr));
    target.Type = AnimationTargetType::SceneNodeTranslation;

    // One target of each type per transform; Value targets are each their own value.
    uint32_t translation = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationTarget(&target, &translation)));
    EXPECT_EQ(0u, translation);
    EXPECT_EQ(Gem::Result::AlreadyExists, pBuilder->AddAnimationTarget(&target, nullptr));
    const uint32_t rotation = AddNodeTarget(pBuilder, node, AnimationTargetType::SceneNodeRotation);
    EXPECT_EQ(1u, rotation);
    EXPECT_EQ(2u, AddValueTarget(pBuilder, AnimationValueType::Float));
    EXPECT_EQ(3u, AddValueTarget(pBuilder, AnimationValueType::Float));

    const float times[2]  = { 0.0f, 1.0f };
    const float values[8] = { 0.0f, 0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 0.0f, 1.0f };
    const float coefficients[12] = {};
    Canvas::AnimationTrackDesc track = Track(AnimationValueType::Float3, times, values, 2);
    uint32_t trackTarget = translation;
    Canvas::AnimationClipDesc clip = Clip("Clip", 1.0f, &track, &trackTarget, 1);

    // A clip needs its track and target arrays, and each track its keys.
    clip.pTrackTargets = nullptr;
    EXPECT_EQ(Gem::Result::BadPointer, pBuilder->AddAnimationClip(&clip, nullptr));
    clip.pTrackTargets = &trackTarget;
    track.pTimes = nullptr;
    EXPECT_EQ(Gem::Result::BadPointer, pBuilder->AddAnimationClip(&clip, nullptr));
    track.pTimes = times;

    // A track's value type and interpolation are known, its key times are in order, and it drives
    // an existing target of its value type.
    track.ValueType = static_cast<AnimationValueType>(5);
    EXPECT_EQ(Gem::Result::InvalidArg, pBuilder->AddAnimationClip(&clip, nullptr));
    track.ValueType = AnimationValueType::Float3;
    track.Interpolation = static_cast<AnimationInterpolation>(3);
    EXPECT_EQ(Gem::Result::InvalidArg, pBuilder->AddAnimationClip(&clip, nullptr));
    track.Interpolation = AnimationInterpolation::Linear;
    const float unsorted[2] = { 1.0f, 0.0f };
    track.pTimes = unsorted;
    EXPECT_EQ(Gem::Result::InvalidArg, pBuilder->AddAnimationClip(&clip, nullptr));
    track.pTimes = times;
    trackTarget = 4;
    EXPECT_EQ(Gem::Result::OutOfRange, pBuilder->AddAnimationClip(&clip, nullptr));
    trackTarget = rotation;
    EXPECT_EQ(Gem::Result::InvalidArg, pBuilder->AddAnimationClip(&clip, nullptr));
    trackTarget = translation;

    // A clip drives each target with at most one track.
    const Canvas::AnimationTrackDesc twoTracks[2] = { track, track };
    const uint32_t sameTarget[2] = { translation, translation };
    const Canvas::AnimationClipDesc twice = Clip("Twice", 1.0f, twoTracks, sameTarget, 2);
    EXPECT_EQ(Gem::Result::InvalidArg, pBuilder->AddAnimationClip(&twice, nullptr));

    // A cubic track of more than one key needs its coefficients, and is not a Quaternion.
    track.Interpolation = AnimationInterpolation::Cubic;
    EXPECT_EQ(Gem::Result::BadPointer, pBuilder->AddAnimationClip(&clip, nullptr));
    track.pCoefficients = coefficients;
    trackTarget = rotation;
    track.ValueType = AnimationValueType::Quaternion;
    EXPECT_EQ(Gem::Result::InvalidArg, pBuilder->AddAnimationClip(&clip, nullptr));
    track.ValueType = AnimationValueType::Float3;
    trackTarget = translation;
    uint32_t clipIndex = InvalidIndex;
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&clip, &clipIndex)));
    EXPECT_EQ(0u, clipIndex);    // the rejected clips added nothing

    track.pCoefficients = nullptr;
    track.KeyCount = 1;
    EXPECT_TRUE(Succeeded(pBuilder->AddAnimationClip(&clip, &clipIndex)));
    EXPECT_EQ(1u, clipIndex);

    EXPECT_NE(BuildModel(pBuilder).Get(), nullptr);
}

// Instantiate writes every field of the result, so a result reused for a model without clips
// holds no controller from an earlier instance.
TEST(CanvasInterfacesTest, ModelInstantiateResultHasNoStaleController)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pAnimatedBuilder = BeginModel(pCanvas, pDevice, "Animated");
    ASSERT_NE(pAnimatedBuilder.Get(), nullptr);
    AddNode(pAnimatedBuilder, "Node", InvalidIndex);
    const uint32_t target = AddValueTarget(pAnimatedBuilder, Canvas::AnimationValueType::Float);
    const float times[1]  = { 0.0f };
    const float values[1] = { 1.0f };
    const Canvas::AnimationTrackDesc track = Track(Canvas::AnimationValueType::Float, times, values, 1);
    const Canvas::AnimationClipDesc clip = Clip("Clip", 1.0f, &track, &target, 1);
    EXPECT_TRUE(Succeeded(pAnimatedBuilder->AddAnimationClip(&clip, nullptr)));
    auto pAnimated = BuildModel(pAnimatedBuilder);
    ASSERT_NE(pAnimated.Get(), nullptr);

    auto pStillBuilder = BeginModel(pCanvas, pDevice, "Still");
    ASSERT_NE(pStillBuilder.Get(), nullptr);
    AddNode(pStillBuilder, "Node", InvalidIndex);
    auto pStill = BuildModel(pStillBuilder);
    ASSERT_NE(pStill.Get(), nullptr);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "ReuseWorld")));
    Canvas::ModelInstantiateResult result{};
    EXPECT_NE(InstantiateAnimated(pAnimated.Get(), pWorld.Get(), result), nullptr);
    EXPECT_EQ(InstantiateAnimated(pStill.Get(), pWorld.Get(), result), nullptr);
    EXPECT_NE(result.pInstanceRoot, nullptr);
}

}
