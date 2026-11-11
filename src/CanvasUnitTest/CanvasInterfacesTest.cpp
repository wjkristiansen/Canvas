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

// Creates an instance of pModel, adds it to pWorld, and places it under pParent, or leaves it
// not placed when pParent is nullptr.
Gem::Result InstantiateModel(Canvas::XWorld *pWorld, Canvas::XModel *pModel, Canvas::XSceneNode *pParent,
                             Canvas::XWorldObject **ppObject)
{
    if (!ppObject)
        return Gem::Result::BadPointer;
    Gem::TGemPtr<Canvas::XWorldObject> pObject;
    Gem::Result result = pWorld->GetCanvas()->CreateWorldObject(pModel, &pObject);
    if (Gem::Succeeded(result))
        result = pWorld->AddWorldObject(pObject);
    if (Gem::Succeeded(result) && pParent)
        result = pWorld->PlaceObject(pObject, pParent, false);
    if (Gem::Succeeded(result))
        *ppObject = pObject.Detach();
    return result;
}

// Creates a world object in pWorld whose root node is pRootNode, or that has no root node when
// pRootNode is nullptr.  A root node that has a parent keeps its place among its siblings.
Gem::Result MakeWorldObject(Canvas::XWorld *pWorld, Canvas::XSceneNode *pRootNode,
                            Canvas::XWorldObject **ppObject, const char *name = nullptr)
{
    if (!ppObject)
        return Gem::Result::BadPointer;
    Gem::TGemPtr<Canvas::XWorldObject> pObject;
    Gem::Result result = pWorld->GetCanvas()->CreateWorldObject(nullptr, &pObject, name);
    if (Gem::Succeeded(result) && pRootNode)
    {
        Gem::TGemPtr<Canvas::XSceneNode> pHold = pRootNode;
        Canvas::XSceneNode *pParent = pRootNode->GetParent();
        Canvas::XSceneNode *pNext = pParent ? pParent->GetNextChild(pRootNode) : nullptr;
        if (pParent)
            result = pParent->RemoveChild(pRootNode);
        if (Gem::Succeeded(result))
            result = pObject->SetRootNode(pRootNode);
        if (pParent)
        {
            const Gem::Result restored = pNext ? pParent->InsertChildBefore(pRootNode, pNext) : pParent->AddChild(pRootNode);
            if (Gem::Succeeded(result))
                result = restored;
        }
    }
    if (Gem::Succeeded(result))
        result = pWorld->AddWorldObject(pObject);
    if (Gem::Succeeded(result))
        *ppObject = pObject.Detach();
    return result;
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

    // Create an empty XSceneNode
    Gem::TGemPtr<Canvas::XSceneNode> pSceneNode;
    const char szNodeName[] = "NullNode";
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pSceneNode)));
    pSceneNode->SetName(szNodeName);

    // Verify QI for XGeneric
    Gem::TGemPtr<XGeneric> pGeneric;
    EXPECT_TRUE(Succeeded(pSceneNode->QueryInterface(&pGeneric)));

    // Validate the name
    EXPECT_TRUE(0 == strncmp(pSceneNode->GetName(), szNodeName, _countof(szNodeName)));
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
    Canvas::XSceneNode *pRoot = pWorld->GetRootNode();

    // A windmill with an arrow in one of its blades, beside hand-built ground
    Gem::TGemPtr<Canvas::XSceneNode> pWindmillNode, pShaft, pBlade1, pBlade2, pGround, pArrowNode, pTip;
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pWindmillNode, "Windmill")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pShaft, "Shaft")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pBlade1, "Blade1")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pBlade2, "Blade2")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pGround, "Ground")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pArrowNode, "Arrow")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pTip, "Tip")));
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
    ASSERT_TRUE(Succeeded(MakeWorldObject(pWorld, pWindmillNode, &pWindmill, "Windmill")));
    ASSERT_TRUE(Succeeded(MakeWorldObject(pWorld, pArrowNode, &pArrow)));  // unnamed
    EXPECT_EQ(pWindmillNode.Get(), pWindmill->GetRootNode());
    EXPECT_EQ(pWorld.Get(), pWindmill->GetWorld());
    EXPECT_STREQ("Windmill", pWindmill->GetName());

    // A root node belongs to its world object; other nodes inherit their parent's
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

// FindChild finds a node's first child with a name.  FindNode finds the first node with a name
// among a world object's own nodes, in pre-order, leaving out the nodes of the world objects
// attached to them.
TEST(CanvasInterfacesTest, WorldObjectFindNode)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "FindWorld")));

    // Windmill > Frame > Shaft > { Blade, Blade > Arrow > Tip }, with a Blade under Frame too
    auto make = [&](const char *name, Canvas::XSceneNode *pParent)
    {
        Gem::TGemPtr<Canvas::XSceneNode> pNode;
        EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pNode, name)));
        if (pParent)
            EXPECT_TRUE(Succeeded(pParent->AddChild(pNode)));
        return pNode;
    };
    auto pWindmillNode = make("Windmill", nullptr);
    auto pFrame        = make("Frame", pWindmillNode);
    auto pShaft        = make("Shaft", pFrame);
    auto pBlade1       = make("Blade", pShaft);
    auto pBlade2       = make("Blade", pShaft);
    auto pSpareBlade   = make("Blade", pFrame);
    auto pArrowNode    = make("Arrow", nullptr);
    auto pTip          = make("Tip", pArrowNode);

    Gem::TGemPtr<Canvas::XWorldObject> pWindmill, pArrow;
    ASSERT_TRUE(Succeeded(MakeWorldObject(pWorld, pWindmillNode, &pWindmill, "Windmill")));
    ASSERT_TRUE(Succeeded(MakeWorldObject(pWorld, pArrowNode, &pArrow, "Arrow")));
    ASSERT_TRUE(Succeeded(pWorld->PlaceObject(pWindmill, pWorld->GetRootNode(), false)));
    ASSERT_TRUE(Succeeded(pWorld->PlaceObject(pArrow, pBlade2, false)));

    // Direct children only, first by name
    EXPECT_EQ(pShaft.Get(), pFrame->FindChild("Shaft"));
    EXPECT_EQ(pBlade1.Get(), pShaft->FindChild("Blade"));
    EXPECT_EQ(pSpareBlade.Get(), pFrame->FindChild("Blade"));
    EXPECT_EQ(nullptr, pWindmillNode->FindChild("Shaft"));
    EXPECT_EQ(nullptr, pFrame->FindChild(nullptr));
    EXPECT_EQ(pWindmillNode.Get(), pWorld->GetRootNode()->FindChild("Windmill"));

    // The world object's own nodes, root node included, in pre-order
    EXPECT_EQ(pWindmillNode.Get(), pWindmill->FindNode("Windmill"));
    EXPECT_EQ(pShaft.Get(), pWindmill->FindNode("Shaft"));
    EXPECT_EQ(pBlade1.Get(), pWindmill->FindNode("Blade"));
    EXPECT_EQ(nullptr, pWindmill->FindNode("Tip"));
    EXPECT_EQ(nullptr, pWindmill->FindNode("Arrow"));
    EXPECT_EQ(pTip.Get(), pArrow->FindNode("Tip"));
    EXPECT_EQ(nullptr, pWindmill->FindNode(nullptr));

    Gem::TGemPtr<Canvas::XWorldObject> pRules;
    ASSERT_TRUE(Succeeded(MakeWorldObject(pWorld, nullptr, &pRules, "Rules")));
    EXPECT_EQ(nullptr, pRules->FindNode("Windmill"));
}

TEST(CanvasInterfacesTest, WorldObjectPlacementStates)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "StatesWorld")));

    // A world object with no root node has no nodes, and is not placed
    Gem::TGemPtr<Canvas::XWorldObject> pRules;
    ASSERT_TRUE(Succeeded(MakeWorldObject(pWorld, nullptr, &pRules, "Rules")));
    EXPECT_EQ(nullptr, pRules->GetRootNode());
    EXPECT_EQ(pWorld.Get(), pRules->GetWorld());

    // A world object whose root node is in no scene graph is not placed; adding its root node
    // to the graph places it
    Gem::TGemPtr<Canvas::XSceneNode> pProjectileNode, pTrail;
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pProjectileNode, "Projectile")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pTrail, "Trail")));
    ASSERT_TRUE(Succeeded(pProjectileNode->AddChild(pTrail)));
    Gem::TGemPtr<Canvas::XWorldObject> pProjectile;
    ASSERT_TRUE(Succeeded(MakeWorldObject(pWorld, pProjectileNode, &pProjectile, "Projectile")));
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

    EXPECT_EQ(Gem::Result::BadPointer, pCanvas->CreateWorldObject(nullptr, nullptr));

    // A new world object is empty and in no world
    Gem::TGemPtr<Canvas::XWorldObject> pObject, pSecond;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorldObject(nullptr, &pObject, "Object")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorldObject(nullptr, &pSecond, "Second")));
    EXPECT_EQ(nullptr, pObject->GetWorld());
    EXPECT_EQ(nullptr, pObject->GetRootNode());
    EXPECT_EQ(Gem::Result::NotFound, pWorld->PlaceObject(pObject, pWorld->GetRootNode(), false));
    EXPECT_EQ(Gem::Result::NotFound, pWorld->SetUniqueName(pObject, "Object"));

    // A root node has no parent and is the root node of one world object at most
    Gem::TGemPtr<Canvas::XSceneNode> pNode, pPlaced;
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pNode, "Node")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pPlaced, "Placed")));
    ASSERT_TRUE(Succeeded(pWorld->GetRootNode()->AddChild(pPlaced)));
    EXPECT_EQ(Gem::Result::InvalidArg, pObject->SetRootNode(pPlaced));
    ASSERT_TRUE(Succeeded(pObject->SetRootNode(pNode)));
    EXPECT_EQ(Gem::Result::InvalidArg, pSecond->SetRootNode(pNode));
    EXPECT_EQ(nullptr, pSecond->GetRootNode());
    EXPECT_EQ(pObject.Get(), pNode->GetWorldObject());

    // A world object is in one world at most
    EXPECT_EQ(Gem::Result::BadPointer, pWorld->AddWorldObject(nullptr));
    ASSERT_TRUE(Succeeded(pWorld->AddWorldObject(pObject)));
    EXPECT_EQ(pWorld.Get(), pObject->GetWorld());
    EXPECT_EQ(Gem::Result::AlreadyExists, pWorld->AddWorldObject(pObject));
    EXPECT_EQ(Gem::Result::AlreadyExists, pOtherWorld->AddWorldObject(pObject));

    // A world's root node is not the root node of a world object, in that world or in none
    EXPECT_EQ(Gem::Result::InvalidArg, pObject->SetRootNode(pWorld->GetRootNode()));
    EXPECT_EQ(pNode.Get(), pObject->GetRootNode());
    EXPECT_EQ(Gem::Result::InvalidArg, pSecond->SetRootNode(pOtherWorld->GetRootNode()));
    EXPECT_EQ(Gem::Result::InvalidArg, pSecond->SetRootNode(pWorld->GetRootNode()));
    EXPECT_EQ(nullptr, pSecond->GetRootNode());

    // A world object is placed under a node of its own canvas
    Gem::TGemPtr<Canvas::XCanvas> pForeignCanvas;
    ASSERT_TRUE(Succeeded(Canvas::CreateCanvas(nullptr, &pForeignCanvas)));
    Gem::TGemPtr<Canvas::XSceneNode> pForeignNode;
    ASSERT_TRUE(Succeeded(pForeignCanvas->CreateSceneNode(&pForeignNode, "Foreign")));
    EXPECT_EQ(Gem::Result::InvalidArg, pWorld->PlaceObject(pObject, pForeignNode, false));
    EXPECT_EQ(nullptr, pNode->GetParent());
}

TEST(CanvasInterfacesTest, WorldObjectLifetime)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<Canvas::XWorldObject> pObject;
    Gem::TGemPtr<Canvas::XSceneNode> pObjectRoot, pChild;
    {
        Gem::TGemPtr<Canvas::XWorld> pWorld;
        ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "ShortWorld")));
        ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pObjectRoot, "Survivor")));
        ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pChild, "Child")));
        ASSERT_TRUE(Succeeded(pObjectRoot->AddChild(pChild)));
        ASSERT_TRUE(Succeeded(pWorld->GetRootNode()->AddChild(pObjectRoot)));
        ASSERT_TRUE(Succeeded(MakeWorldObject(pWorld, pObjectRoot, &pObject, "Survivor")));
    }

    // A world object the app holds outlives its world, with its root node and the
    // nodes below it; the root node no longer has a parent
    EXPECT_EQ(nullptr, pObject->GetWorld());
    EXPECT_EQ(pObjectRoot.Get(), pObject->GetRootNode());
    EXPECT_EQ(nullptr, pObjectRoot->GetParent());
    EXPECT_EQ(pObject.Get(), pObjectRoot->GetWorldObject());
    EXPECT_EQ(pObject.Get(), pChild->GetWorldObject());
    (void)pChild->GetGlobalMatrix();

    // Releasing the world object clears its root node's back-reference
    pObject = nullptr;
    EXPECT_EQ(nullptr, pObjectRoot->GetWorldObject());
    EXPECT_EQ(nullptr, pChild->GetWorldObject());
}

// Replacing a placed world object's root node puts the new root node in the old one's place, and
// the old root node, with its subtree, leaves the scene graph and the world object.
TEST(CanvasInterfacesTest, WorldObjectSetRootNode)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "SwapWorld")));
    Canvas::XSceneNode *pRoot = pWorld->GetRootNode();

    Gem::TGemPtr<Canvas::XSceneNode> pBefore, pStanding, pStandingArm, pFallen, pAfter;
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pBefore, "Before")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pStanding, "Standing")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pStandingArm, "StandingArm")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pFallen, "Fallen")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pAfter, "After")));
    ASSERT_TRUE(Succeeded(pStanding->AddChild(pStandingArm)));
    ASSERT_TRUE(Succeeded(pRoot->AddChild(pBefore)));

    Gem::TGemPtr<Canvas::XWorldObject> pStatue;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorldObject(nullptr, &pStatue, "Statue")));
    ASSERT_TRUE(Succeeded(pStatue->SetRootNode(pStanding)));
    ASSERT_TRUE(Succeeded(pWorld->AddWorldObject(pStatue)));
    ASSERT_TRUE(Succeeded(pWorld->PlaceObject(pStatue, pRoot, false)));
    ASSERT_TRUE(Succeeded(pRoot->AddChild(pAfter)));

    // The fallen tree takes the standing tree's place between its siblings
    ASSERT_TRUE(Succeeded(pStatue->SetRootNode(pFallen)));
    EXPECT_EQ(pFallen.Get(), pStatue->GetRootNode());
    EXPECT_EQ(pFallen.Get(), pRoot->GetNextChild(pBefore));
    EXPECT_EQ(pAfter.Get(), pRoot->GetNextChild(pFallen));
    EXPECT_EQ(pStatue.Get(), pFallen->GetWorldObject());
    EXPECT_EQ(nullptr, pStanding->GetParent());
    EXPECT_EQ(nullptr, pStandingArm->GetWorldObject());

    // The app keeps the standing tree and swaps it back
    ASSERT_TRUE(Succeeded(pStatue->SetRootNode(pStanding)));
    EXPECT_EQ(pStanding.Get(), pRoot->GetNextChild(pBefore));
    EXPECT_EQ(pStatue.Get(), pStandingArm->GetWorldObject());
    EXPECT_EQ(nullptr, pFallen->GetParent());

    // Clearing the root node takes it out of the scene graph
    ASSERT_TRUE(Succeeded(pStatue->SetRootNode(nullptr)));
    EXPECT_EQ(nullptr, pStatue->GetRootNode());
    EXPECT_EQ(pAfter.Get(), pRoot->GetNextChild(pBefore));
    EXPECT_EQ(nullptr, pStanding->GetWorldObject());
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));
}

// Releasing a world object takes it out of its world, which releases its unique name, and it can
// then be added to another world.
TEST(CanvasInterfacesTest, WorldObjectMovesBetweenWorlds)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    Gem::TGemPtr<Canvas::XWorld> pLevel, pNextLevel;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pLevel, "Level")));
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pNextLevel, "NextLevel")));

    Gem::TGemPtr<Canvas::XSceneNode> pNode;
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pNode, "Hero")));
    Gem::TGemPtr<Canvas::XWorldObject> pHero;
    ASSERT_TRUE(Succeeded(MakeWorldObject(pLevel, pNode, &pHero, "Hero")));
    ASSERT_TRUE(Succeeded(pLevel->PlaceObject(pHero, pLevel->GetRootNode(), false)));
    ASSERT_TRUE(Succeeded(pLevel->SetUniqueName(pHero, "Hero")));

    ASSERT_TRUE(Succeeded(pLevel->ReleaseWorldObject(pHero)));
    EXPECT_EQ(nullptr, pHero->GetWorld());
    EXPECT_EQ(nullptr, pHero->GetUniqueName());
    EXPECT_EQ(nullptr, pLevel->FindObject("Hero"));
    EXPECT_EQ(nullptr, pNode->GetParent());
    EXPECT_EQ(pNode.Get(), pHero->GetRootNode());
    EXPECT_EQ(Gem::Result::NotFound, pLevel->ReleaseWorldObject(pHero));

    ASSERT_TRUE(Succeeded(pNextLevel->AddWorldObject(pHero)));
    EXPECT_EQ(pNextLevel.Get(), pHero->GetWorld());
    ASSERT_TRUE(Succeeded(pNextLevel->PlaceObject(pHero, pNextLevel->GetRootNode(), false)));
    ASSERT_TRUE(Succeeded(pNextLevel->SetUniqueName(pHero, "Hero")));
    EXPECT_EQ(pHero.Get(), pNextLevel->FindObject("Hero"));
    EXPECT_TRUE(Succeeded(pNextLevel->Update(0.0f)));
}

TEST(CanvasInterfacesTest, SceneNodesTest)
{
    // Create XCanvas object
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    // Create XWorld object
    Gem::TGemPtr<XWorld> pWorld;
    EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld)));

    // Create nodes
    Gem::TGemPtr<XSceneNode> pNodes[6];
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pNodes[0])));
    pNodes[0]->SetName("Node0");
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pNodes[1])));
    pNodes[1]->SetName("Node1");
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pNodes[2])));
    pNodes[2]->SetName("Node2");
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pNodes[3])));
    pNodes[3]->SetName("Node3");
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pNodes[4])));
    pNodes[4]->SetName("Node4");
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pNodes[5])));
    pNodes[5]->SetName("Node5");

    // Build the following tree
    // Root
    //  - Node0
    //      - Node2
    //      - Node3
    //  - Node1
    //      - Node4
    //          - Node5

    Gem::TGemPtr<XSceneNode> pRoot = pWorld->GetRootNode();
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
    Gem::TGemPtr<XSceneNode> pA, pB;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pA)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pB)));

    Gem::TGemPtr<XSceneNode> pRoot = pWorld->GetRootNode();
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
    Gem::TGemPtr<XSceneNode> pC;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pC)));
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

    Gem::TGemPtr<XSceneNode> pParent, pChild;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pParent)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pChild)));
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

    Gem::TGemPtr<XSceneNode> pParent, pChild;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pParent)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pChild)));
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
    Gem::TGemPtr<XSceneNode> pA, pB, pCameraNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pA)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pB)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pCameraNode)));

    Gem::TGemPtr<XSceneNode> pRoot = pWorld->GetRootNode();
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
    Gem::TGemPtr<XSceneNode> pA, pB, pCameraNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pA)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pB)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pCameraNode)));

    Gem::TGemPtr<XSceneNode> pRoot = pWorld->GetRootNode();
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
    Gem::TGemPtr<XSceneNode> pCameraNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pCameraNode)));

    Gem::TGemPtr<XSceneNode> pRoot = pWorld->GetRootNode();
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
    Gem::TGemPtr<XSceneNode> pA, pB, pLightNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pA)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pB)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pLightNode)));

    Gem::TGemPtr<XSceneNode> pRoot = pWorld->GetRootNode();
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
    Gem::TGemPtr<XSceneNode> pA, pB, pLightNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pA)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pB)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pLightNode)));

    Gem::TGemPtr<XSceneNode> pRoot = pWorld->GetRootNode();

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
    Gem::TGemPtr<XSceneNode> pParent, pLightNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pParent)));
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pLightNode)));

    Gem::TGemPtr<XSceneNode> pRoot = pWorld->GetRootNode();
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
T *FindElement(Canvas::XSceneNode *pNode)
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

    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));

    // The instance is a world object in the world, whose root node is the instance root
    ASSERT_NE(pInstance.Get(), nullptr);
    EXPECT_TRUE(pInstance->GetWorld() == pWorld.Get());
    EXPECT_STREQ("TestModel", pInstance->GetName());
    EXPECT_NE(pInstance->GetRootNode(), nullptr);
    EXPECT_TRUE(pInstance->GetRootNode()->GetWorldObject() == pInstance.Get());
    EXPECT_EQ(pInstance->GetRootNode()->GetAnimationController(), nullptr); // no clips in model

    // Instance root should be a child of scene root
    Canvas::XSceneNode *pSceneRoot = pWorld->GetRootNode();
    EXPECT_TRUE(pSceneRoot->GetFirstChild() == pInstance->GetRootNode());

    // Instance root should have 2 children (NodeA and NodeB)
    Canvas::XSceneNode *pNodeA = pInstance->GetRootNode()->GetFirstChild();
    ASSERT_NE(pNodeA, nullptr);
    Canvas::XSceneNode *pNodeB = pInstance->GetRootNode()->GetNextChild(pNodeA);
    ASSERT_NE(pNodeB, nullptr);
    EXPECT_EQ(pInstance->GetRootNode()->GetNextChild(pNodeB), nullptr); // no more children
    EXPECT_STREQ("NodeA", pNodeA->GetName());
    EXPECT_STREQ("NodeB", pNodeB->GetName());

    // Transforms come from the model nodes
    EXPECT_TRUE(AlmostEqual(pNodeA->GetLocalTranslation(), FloatVector4(1.0f, 0.0f, 0.0f, 1.0f)));
    EXPECT_TRUE(AlmostEqual(pNodeB->GetLocalTranslation(), FloatVector4(0.0f, 2.0f, 0.0f, 1.0f)));

    // NodeB should have one child (NodeC)
    Canvas::XSceneNode *pNodeC = pNodeB->GetFirstChild();
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

    Gem::TGemPtr<Canvas::XWorldObject> pInstance1, pInstance2;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance1)));
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance2)));

    // Both instances should exist independently
    EXPECT_NE(pInstance1->GetRootNode(), nullptr);
    EXPECT_NE(pInstance2->GetRootNode(), nullptr);
    EXPECT_TRUE(pInstance1->GetRootNode() != pInstance2->GetRootNode());

    // Modify instance 1's child transform
    Canvas::XSceneNode *pNode1A = pInstance1->GetRootNode()->GetFirstChild();
    Canvas::XSceneNode *pNode2A = pInstance2->GetRootNode()->GetFirstChild();
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

    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));
    Canvas::XSceneNode *pNode = pInstance->GetRootNode()->GetFirstChild();
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

    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));

    // Instantiation leaves the world's active camera to the app
    EXPECT_EQ(pWorld->GetActiveCamera(), nullptr);

    // The instance's camera is on the instance's scene node for the camera's model node, and
    // carries the model camera's name and properties
    Canvas::XSceneNode *pCamNode = pInstance->GetRootNode()->GetFirstChild();
    ASSERT_NE(pCamNode, nullptr);
    Canvas::XCamera *pCamera = FindElement<Canvas::XCamera>(pCamNode);
    ASSERT_NE(pCamera, nullptr);
    EXPECT_STREQ("ModelCamera", pCamera->GetName());
    EXPECT_EQ(1.2f, pCamera->GetFovAngle());
    EXPECT_EQ(0.5f, pCamera->GetNearClip());
    EXPECT_EQ(500.0f, pCamera->GetFarClip());
    EXPECT_TRUE(pCamera->GetAttachedNode() == pCamNode);
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

    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));

    Canvas::XSceneNode *pLightNode = pInstance->GetRootNode()->GetFirstChild();
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

// Creating a world object from a model instantiates it, in no world and with no parent; adding it
// to a world leaves it not placed until it is placed.
TEST(CanvasInterfacesTest, ModelInstantiateNotPlaced)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pBuilder = BeginModel(pCanvas, pDevice, "TestModel");
    ASSERT_NE(pBuilder.Get(), nullptr);
    AddNode(pBuilder, "Node", InvalidIndex);
    auto pModel = BuildModel(pBuilder);
    ASSERT_NE(pModel.Get(), nullptr);

    Gem::TGemPtr<Canvas::XWorld> pWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "TestWorld")));

    Gem::TGemPtr<Canvas::XWorldObject> pInstance, pNamed;
    EXPECT_EQ(Gem::Result::BadPointer, pCanvas->CreateWorldObject(pModel, nullptr));
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorldObject(pModel, &pInstance)));
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorldObject(pModel, &pNamed, "Named")));
    EXPECT_STREQ("TestModel", pInstance->GetName());
    EXPECT_STREQ("Named", pNamed->GetName());
    EXPECT_EQ(nullptr, pInstance->GetWorld());
    ASSERT_NE(pInstance->GetRootNode(), nullptr);
    EXPECT_EQ(pInstance->GetRootNode()->GetParent(), nullptr);
    EXPECT_NE(pInstance->GetRootNode()->GetFirstChild(), nullptr);
    EXPECT_TRUE(pInstance->GetRootNode()->GetFirstChild()->GetWorldObject() == pInstance.Get());

    ASSERT_TRUE(Succeeded(pWorld->AddWorldObject(pInstance)));
    EXPECT_TRUE(pInstance->GetWorld() == pWorld.Get());
    EXPECT_EQ(pWorld->GetRootNode()->GetFirstChild(), nullptr);

    // Placing the instance puts it in the scene graph
    EXPECT_TRUE(Succeeded(pWorld->PlaceObject(pInstance, pWorld->GetRootNode(), false)));
    EXPECT_TRUE(pWorld->GetRootNode()->GetFirstChild() == pInstance->GetRootNode());
}

// A skeleton instance composes its bones parents first, in the space of its node, and
// moves its sockets (and the content under them) with their bones.  Scene nodes
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
    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));

    Canvas::XSceneNode *pArmature = pInstance->GetRootNode()->GetFirstChild();
    ASSERT_NE(pArmature, nullptr);
    Canvas::XSceneNode *pSocket = pArmature->GetFirstChild();
    ASSERT_NE(pSocket, nullptr);
    EXPECT_STREQ("KneeSocket", pSocket->GetName());
    Canvas::XSceneNode *pProp = pSocket->GetFirstChild();
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

    // Reference: the same chain as scene nodes, in skeleton space.
    const auto MakeNode = [&](const Canvas::Transform &t, Canvas::XSceneNode *pParent)
    {
        Gem::TGemPtr<Canvas::XSceneNode> pNode;
        EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pNode, "Reference")));
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

    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));
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
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));
    EXPECT_TRUE(AlmostEqual(pSocket->GetGlobalMatrix(), pRefSocket->GetGlobalMatrix() * pArmature->GetGlobalMatrix()));

    // A socket node removed from the skeleton's node keeps its last transform.
    Gem::TGemPtr<Canvas::XSceneNode> pRemoved = pSocket;
    const FloatVector4 lastTranslation = pRemoved->GetLocalTranslation();
    EXPECT_TRUE(Succeeded(pArmature->RemoveChild(pRemoved)));
    EXPECT_TRUE(Succeeded(pSkeleton->SetBoneLocalTransform(1, &bent)));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));
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
    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));

    Canvas::XSceneNode *pArmature = pInstance->GetRootNode()->GetFirstChild();
    ASSERT_NE(pArmature, nullptr);
    Canvas::XSceneNode *pBody = pArmature->GetFirstChild();
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
    pInstance->GetRootNode()->SetLocalTranslation(FloatVector4(0.0f, 0.0f, 7.0f, 0.0f));
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

    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));
    ASSERT_NE(pInstance->GetRootNode()->GetAnimationController(), nullptr);
    ASSERT_NE(pInstance->GetRootNode(), nullptr);

    Canvas::XAnimationController* pCtrl = pInstance->GetRootNode()->GetAnimationController();
    Canvas::XSceneNode*      pNode = pInstance->GetRootNode()->GetFirstChild();
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

// XWorld::Update updates each active world object once, wherever its nodes are: in the scene
// graph, attached to another world object's node, or not placed.  An inactive world object in
// the scene graph is left as it is, and the world objects attached to its nodes are still updated.
TEST(CanvasInterfacesTest, WorldUpdateAdvancesWorldObjectsNotPlaced)
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

    // One instance in the scene graph, one attached to its node, and one not placed
    Gem::TGemPtr<Canvas::XWorldObject> pShown, pRider, pPooled;
    ASSERT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pShown)));
    ASSERT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pShown->GetRootNode()->GetFirstChild(), &pRider)));
    ASSERT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, nullptr, &pPooled)));
    Canvas::XAnimationController *pShownCtrl = pShown->GetRootNode()->GetAnimationController();
    Canvas::XAnimationController *pRiderCtrl = pRider->GetRootNode()->GetAnimationController();
    Canvas::XAnimationController *pPooledCtrl = pPooled->GetRootNode()->GetAnimationController();
    ASSERT_NE(pShownCtrl, nullptr);
    ASSERT_NE(pRiderCtrl, nullptr);
    ASSERT_NE(pPooledCtrl, nullptr);
    EXPECT_TRUE(pShownCtrl != pRiderCtrl);

    ASSERT_TRUE(Succeeded(pShownCtrl->Play(0, 0.0f)));
    ASSERT_TRUE(Succeeded(pRiderCtrl->Play(0, 0.0f)));
    ASSERT_TRUE(Succeeded(pPooledCtrl->Play(0, 0.0f)));
    ASSERT_TRUE(Succeeded(pWorld->Update(0.5f)));

    EXPECT_FLOAT_EQ(0.5f, pShownCtrl->GetTime());
    EXPECT_FLOAT_EQ(0.5f, pRiderCtrl->GetTime());
    EXPECT_FLOAT_EQ(0.5f, pPooledCtrl->GetTime());

    ASSERT_TRUE(Succeeded(pWorld->DeactivateObject(pShown)));
    ASSERT_TRUE(Succeeded(pWorld->Update(0.5f)));

    EXPECT_FLOAT_EQ(0.5f, pShownCtrl->GetTime());
    EXPECT_FLOAT_EQ(1.0f, pRiderCtrl->GetTime());
    EXPECT_FLOAT_EQ(1.0f, pPooledCtrl->GetTime());
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

    // Three pooled projectiles, each a world object that is not placed, playing the same clip
    constexpr int kCount = 3;
    Gem::TGemPtr<Canvas::XWorldObject> pObjects[kCount];
    Canvas::XAnimationController *pControllers[kCount] = {};
    for (int i = 0; i < kCount; ++i)
    {
        ASSERT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, nullptr, &pObjects[i])));
        pControllers[i] = pObjects[i]->GetRootNode()->GetAnimationController();
        ASSERT_NE(pControllers[i], nullptr);
        ASSERT_TRUE(Succeeded(pControllers[i]->Play(0, 0.0f)));
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

namespace
{
// A node named name, with the given local translation, added as the last child of pParent when
// pParent is not nullptr.
Gem::TGemPtr<Canvas::XSceneNode> MakeNode(XCanvas *pCanvas, Canvas::XSceneNode *pParent,
                                               const char *name, float x, float y, float z)
{
    Gem::TGemPtr<Canvas::XSceneNode> pNode;
    EXPECT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pNode, name)));
    pNode->SetLocalTransform(Translation(x, y, z));
    if (pParent)
        EXPECT_TRUE(Succeeded(pParent->AddChild(pNode)));
    return pNode;
}

// The elevator example from the spec: an elevator carries a car, and the car its driver.
struct ElevatorWorld
{
    Gem::TGemPtr<Canvas::XWorld> pWorld;
    Gem::TGemPtr<Canvas::XSceneNode> pElevatorNode, pPlatform, pCarNode, pSeat, pDriverNode;
    Gem::TGemPtr<Canvas::XWorldObject> pElevator, pCar, pDriver;

    ElevatorWorld(XCanvas *pCanvas, Canvas::XGfxDevice *pDevice)
    {
        EXPECT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pWorld, "ElevatorWorld")));
        Canvas::XSceneNode *pRoot = pWorld->GetRootNode();
        pElevatorNode = MakeNode(pCanvas, pRoot, "Elevator", 10.0f, 0.0f, 0.0f);
        pPlatform     = MakeNode(pCanvas, pElevatorNode, "Platform", 0.0f, 5.0f, 0.0f);
        pCarNode      = MakeNode(pCanvas, pPlatform, "Car", 1.0f, 0.0f, 0.0f);
        pSeat         = MakeNode(pCanvas, pCarNode, "Seat", 0.0f, 1.0f, 0.0f);
        pDriverNode   = MakeNode(pCanvas, pSeat, "Driver", 0.0f, 0.0f, 0.5f);
        EXPECT_TRUE(Succeeded(MakeWorldObject(pWorld, pElevatorNode, &pElevator, "Elevator")));
        EXPECT_TRUE(Succeeded(MakeWorldObject(pWorld, pCarNode, &pCar, "Car")));
        EXPECT_TRUE(Succeeded(MakeWorldObject(pWorld, pDriverNode, &pDriver, "Driver")));
    }
};
} // anonymous namespace

TEST(CanvasInterfacesTest, WorldObjectPlace)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    ElevatorWorld w(pCanvas, pDevice);
    Canvas::XSceneNode *pRoot = w.pWorld->GetRootNode();

    // The driver at (11, 6, 0.5) gets out onto the ground, keeping its world transform
    auto pGround = MakeNode(pCanvas, pRoot, "Ground", 0.0f, -1.0f, 0.0f);
    EXPECT_TRUE(AlmostEqual(w.pDriverNode->GetGlobalTranslation(), FloatVector4(11.0f, 6.0f, 0.5f, 1.0f)));
    ASSERT_TRUE(Succeeded(w.pWorld->PlaceObject(w.pDriver, pGround, true)));
    EXPECT_EQ(pGround.Get(), w.pDriverNode->GetParent());
    EXPECT_EQ(w.pDriver.Get(), w.pDriverNode->GetWorldObject());
    EXPECT_TRUE(AlmostEqual(w.pDriverNode->GetGlobalTranslation(), FloatVector4(11.0f, 6.0f, 0.5f, 1.0f)));
    EXPECT_TRUE(AlmostEqual(w.pDriverNode->GetLocalTranslation(), FloatVector4(11.0f, 7.0f, 0.5f, 1.0f)));

    // Keeping the world transform under a rotated, scaled, and mirrored parent
    auto pTurntable = MakeNode(pCanvas, pRoot, "Turntable", 3.0f, 2.0f, -4.0f);
    Canvas::Transform turn = pTurntable->GetLocalTransform();
    turn.Rotation = QuaternionFromAngleAxis(FloatVector4(0.0f, 1.0f, 0.0f, 1.2f));
    turn.Scale    = FloatVector4(-2.0f, 2.0f, 2.0f, 0.0f);
    pTurntable->SetLocalTransform(turn);
    w.pDriverNode->SetLocalRotation(QuaternionFromAngleAxis(FloatVector4(1.0f, 0.0f, 0.0f, 0.4f)));
    const FloatMatrix4x4 before = w.pDriverNode->GetGlobalMatrix();
    ASSERT_TRUE(Succeeded(w.pWorld->PlaceObject(w.pDriver, pTurntable, true)));
    const FloatMatrix4x4 after = w.pDriverNode->GetGlobalMatrix();
    for (int r = 0; r < 4; ++r)
        EXPECT_TRUE(AlmostEqual(after[r], before[r])) << "row " << r;

    // Back in the seat, keeping its local transform
    w.pDriverNode->SetLocalTransform(Translation(11.0f, 7.0f, 0.5f));
    ASSERT_TRUE(Succeeded(w.pWorld->PlaceObject(w.pDriver, w.pSeat, false)));
    EXPECT_TRUE(AlmostEqual(w.pDriverNode->GetLocalTranslation(), FloatVector4(11.0f, 7.0f, 0.5f, 1.0f)));
    EXPECT_EQ(w.pDriver.Get(), w.pDriverNode->GetWorldObject());

    // Moving the car moves the driver attached to its seat
    ASSERT_TRUE(Succeeded(w.pWorld->PlaceObject(w.pCar, pGround, false)));
    EXPECT_EQ(pGround.Get(), w.pCarNode->GetParent());
    EXPECT_EQ(w.pSeat.Get(), w.pDriverNode->GetParent());
    EXPECT_EQ(w.pCar.Get(), w.pSeat->GetWorldObject());

    // A world object is placed only outside its root node's subtree
    EXPECT_EQ(Gem::Result::InvalidArg, w.pWorld->PlaceObject(w.pCar, w.pCarNode, false));
    EXPECT_EQ(Gem::Result::InvalidArg, w.pWorld->PlaceObject(w.pCar, w.pDriverNode, false));
    EXPECT_EQ(Gem::Result::InvalidArg, w.pWorld->PlaceObject(w.pCar, nullptr, false));

    // A world object with no root node has nothing to place
    Gem::TGemPtr<Canvas::XWorldObject> pRules;
    ASSERT_TRUE(Succeeded(MakeWorldObject(w.pWorld, nullptr, &pRules)));
    EXPECT_EQ(Gem::Result::InvalidArg, w.pWorld->PlaceObject(pRules, pGround, false));

    // Only the world's own world objects
    Gem::TGemPtr<Canvas::XWorld> pOtherWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pOtherWorld, "OtherWorld")));
    EXPECT_EQ(Gem::Result::NotFound, pOtherWorld->PlaceObject(w.pCar, pGround, false));
}

TEST(CanvasInterfacesTest, WorldObjectDetach)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    ElevatorWorld w(pCanvas, pDevice);

    // The camera rides in the car
    Gem::TGemPtr<Canvas::XCamera> pCamera;
    ASSERT_TRUE(Succeeded(pCanvas->CreateCamera(&pCamera, "CarCamera")));
    ASSERT_TRUE(Succeeded(w.pSeat->BindElement(pCamera)));
    w.pWorld->SetActiveCamera(pCamera);

    // Detaching the car takes it, its driver, and the camera out of the scene graph, keeping
    // the car's local transform; the car stays in the world
    ASSERT_TRUE(Succeeded(w.pWorld->DetachObject(w.pCar)));
    EXPECT_EQ(nullptr, w.pCarNode->GetParent());
    EXPECT_EQ(w.pWorld.Get(), w.pCar->GetWorld());
    EXPECT_TRUE(AlmostEqual(w.pCarNode->GetLocalTranslation(), FloatVector4(1.0f, 0.0f, 0.0f, 1.0f)));
    EXPECT_EQ(w.pCar.Get(), w.pSeat->GetWorldObject());
    EXPECT_EQ(w.pDriver.Get(), w.pDriverNode->GetWorldObject());
    EXPECT_EQ(w.pSeat.Get(), w.pDriverNode->GetParent());

    // The next Update clears the active camera, which left the scene graph
    EXPECT_EQ(pCamera.Get(), w.pWorld->GetActiveCamera());
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_EQ(nullptr, w.pWorld->GetActiveCamera());

    // Detaching a world object that is not placed changes nothing; placing puts it back
    EXPECT_TRUE(Succeeded(w.pWorld->DetachObject(w.pCar)));
    ASSERT_TRUE(Succeeded(w.pWorld->PlaceObject(w.pCar, w.pPlatform, false)));
    EXPECT_EQ(w.pPlatform.Get(), w.pCarNode->GetParent());
    EXPECT_TRUE(AlmostEqual(w.pDriverNode->GetGlobalTranslation(), FloatVector4(11.0f, 6.0f, 0.5f, 1.0f)));

    // Only the world's own world objects
    Gem::TGemPtr<Canvas::XWorld> pOtherWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pOtherWorld, "OtherWorld")));
    EXPECT_EQ(Gem::Result::NotFound, pOtherWorld->DetachObject(w.pCar));
}

namespace
{
// A WorldListener that records each callback as "orphaned <name>" or "camera lost", and runs
// onEvent after recording it.
struct RecordingListener : Canvas::WorldListener
{
    std::vector<std::string> events;
    std::function<void()> onEvent;

    void OnObjectOrphaned(Canvas::XWorld *, Canvas::XWorldObject *pObject) override
    {
        events.push_back(std::string("orphaned ") + pObject->GetName());
        if (onEvent)
            onEvent();
    }
    void OnActiveCameraLost(Canvas::XWorld *) override
    {
        events.push_back("camera lost");
        if (onEvent)
            onEvent();
    }
};
} // anonymous namespace

TEST(CanvasInterfacesTest, WorldObjectReleaseKeepsOrphans)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    ElevatorWorld w(pCanvas, pDevice);
    RecordingListener listener;
    ASSERT_TRUE(Succeeded(w.pWorld->RegisterListener(&listener)));

    // The camera is on the driver, who is kept
    Gem::TGemPtr<Canvas::XCamera> pCamera;
    ASSERT_TRUE(Succeeded(pCanvas->CreateCamera(&pCamera, "DriverCamera")));
    ASSERT_TRUE(Succeeded(w.pDriverNode->BindElement(pCamera)));
    w.pWorld->SetActiveCamera(pCamera);

    // Removing the car leaves its driver on the platform, where it was
    ASSERT_TRUE(Succeeded(w.pWorld->ReleaseWorldObject(w.pCar)));
    EXPECT_EQ(nullptr, w.pCar->GetWorld());
    EXPECT_EQ(nullptr, w.pCarNode->GetParent());
    EXPECT_EQ(w.pPlatform.Get(), w.pDriverNode->GetParent());
    EXPECT_EQ(w.pElevator.Get(), w.pDriverNode->GetParent()->GetWorldObject());
    EXPECT_EQ(w.pWorld.Get(), w.pDriver->GetWorld());
    EXPECT_TRUE(AlmostEqual(w.pDriverNode->GetGlobalTranslation(), FloatVector4(11.0f, 6.0f, 0.5f, 1.0f)));
    EXPECT_EQ(pCamera.Get(), w.pWorld->GetActiveCamera());

    // The released world object is no longer one of the world's
    EXPECT_EQ(Gem::Result::NotFound, w.pWorld->ReleaseWorldObject(w.pCar));

    // The removal delivers no callback; the next Update does, once
    EXPECT_TRUE(listener.events.empty());
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_EQ(std::vector<std::string>({ "orphaned Driver" }), listener.events);
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_EQ(1u, listener.events.size());
}

TEST(CanvasInterfacesTest, WorldObjectReleaseTopLevel)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    ElevatorWorld w(pCanvas, pDevice);
    RecordingListener listener;
    ASSERT_TRUE(Succeeded(w.pWorld->RegisterListener(&listener)));

    // The camera rides on the platform, which goes with the elevator
    Gem::TGemPtr<Canvas::XCamera> pCamera;
    ASSERT_TRUE(Succeeded(pCanvas->CreateCamera(&pCamera, "PlatformCamera")));
    ASSERT_TRUE(Succeeded(w.pPlatform->BindElement(pCamera)));
    w.pWorld->SetActiveCamera(pCamera);

    // Removing the elevator orphans the car, which lands where the elevator was attached: the
    // root node, keeping its world transform.  The driver stays in the car.
    ASSERT_TRUE(Succeeded(w.pWorld->ReleaseWorldObject(w.pElevator)));
    EXPECT_EQ(nullptr, w.pElevator->GetWorld());
    EXPECT_EQ(w.pWorld.Get(), w.pCar->GetWorld());
    EXPECT_EQ(w.pWorld->GetRootNode(), w.pCarNode->GetParent());
    EXPECT_EQ(w.pSeat.Get(), w.pDriverNode->GetParent());
    EXPECT_TRUE(AlmostEqual(w.pDriverNode->GetGlobalTranslation(), FloatVector4(11.0f, 6.0f, 0.5f, 1.0f)));

    // An orphan released before the next Update gets no callback.  The next Update clears the
    // active camera and reports its loss.
    ASSERT_TRUE(Succeeded(w.pWorld->ReleaseWorldObject(w.pCar)));
    EXPECT_EQ(pCamera.Get(), w.pWorld->GetActiveCamera());
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_EQ(nullptr, w.pWorld->GetActiveCamera());
    EXPECT_EQ(std::vector<std::string>({ "orphaned Driver", "camera lost" }), listener.events);
}

TEST(CanvasInterfacesTest, WorldListenerCallbacks)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    ElevatorWorld w(pCanvas, pDevice);
    RecordingListener first, second;

    // Registration
    EXPECT_EQ(Gem::Result::InvalidArg, w.pWorld->RegisterListener(nullptr));
    ASSERT_TRUE(Succeeded(w.pWorld->RegisterListener(&first)));
    EXPECT_EQ(Gem::Result::AlreadyExists, w.pWorld->RegisterListener(&first));
    ASSERT_TRUE(Succeeded(w.pWorld->RegisterListener(&second)));
    EXPECT_EQ(Gem::Result::NotFound, w.pWorld->UnregisterListener(nullptr));

    // A callback may remove a world object, and the callbacks that causes are delivered by the
    // same Update.  The first listener unregisters the second, which receives nothing after it.
    first.onEvent = [&]
    {
        if (first.events.size() == 1)
        {
            EXPECT_EQ(Gem::Result::Unavailable, w.pWorld->Update(0.0f));
            EXPECT_TRUE(Succeeded(w.pWorld->ReleaseWorldObject(w.pCar)));
            EXPECT_TRUE(Succeeded(w.pWorld->UnregisterListener(&second)));
        }
    };
    ASSERT_TRUE(Succeeded(w.pWorld->ReleaseWorldObject(w.pElevator)));
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_EQ(std::vector<std::string>({ "orphaned Car", "orphaned Driver" }), first.events);
    EXPECT_TRUE(second.events.empty());
    EXPECT_EQ(w.pWorld->GetRootNode(), w.pDriverNode->GetParent());
    EXPECT_EQ(Gem::Result::NotFound, w.pWorld->UnregisterListener(&second));

    // Releasing a world delivers no callback
    first.onEvent = nullptr;
    Gem::TGemPtr<Canvas::XWorldObject> pRider;
    auto pRiderNode = MakeNode(pCanvas, w.pDriverNode, "Rider", 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(Succeeded(MakeWorldObject(w.pWorld, pRiderNode, &pRider, "Rider")));
    ASSERT_TRUE(Succeeded(w.pWorld->ReleaseWorldObject(w.pDriver)));
    w.pWorld = nullptr;
    EXPECT_EQ(2u, first.events.size());
}

TEST(CanvasInterfacesTest, WorldActiveCameraLeavesSceneGraph)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    ElevatorWorld w(pCanvas, pDevice);
    RecordingListener listener;
    ASSERT_TRUE(Succeeded(w.pWorld->RegisterListener(&listener)));

    // A camera with no node is not in the scene graph
    Gem::TGemPtr<Canvas::XCamera> pCamera;
    ASSERT_TRUE(Succeeded(pCanvas->CreateCamera(&pCamera, "Camera")));
    w.pWorld->SetActiveCamera(pCamera);
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_EQ(nullptr, w.pWorld->GetActiveCamera());
    EXPECT_EQ(std::vector<std::string>({ "camera lost" }), listener.events);

    // A camera whose node is destroyed has no node
    {
        auto pNode = MakeNode(pCanvas, w.pWorld->GetRootNode(), "CameraNode", 0.0f, 0.0f, 0.0f);
        ASSERT_TRUE(Succeeded(pNode->BindElement(pCamera)));
    }
    w.pWorld->SetActiveCamera(pCamera);
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_EQ(pCamera.Get(), w.pWorld->GetActiveCamera());
    Canvas::XSceneNode *pRoot = w.pWorld->GetRootNode();
    ASSERT_TRUE(Succeeded(pRoot->RemoveChild(pCamera->GetAttachedNode())));
    EXPECT_EQ(nullptr, pCamera->GetAttachedNode());
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_EQ(nullptr, w.pWorld->GetActiveCamera());
    EXPECT_EQ(2u, listener.events.size());

    // A callback that takes the active camera out of the scene graph has its loss reported by
    // the same Update
    ASSERT_TRUE(Succeeded(w.pSeat->BindElement(pCamera)));
    w.pWorld->SetActiveCamera(pCamera);
    listener.onEvent = [&]
    {
        if (listener.events.back() == "orphaned Car")
            EXPECT_TRUE(Succeeded(w.pWorld->DetachObject(w.pCar)));
    };
    ASSERT_TRUE(Succeeded(w.pWorld->ReleaseWorldObject(w.pElevator)));
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_EQ(nullptr, w.pWorld->GetActiveCamera());
    EXPECT_EQ(std::vector<std::string>({ "camera lost", "camera lost", "orphaned Car", "camera lost" }),
              listener.events);
}

TEST(CanvasInterfacesTest, WorldRootNestedInAnotherWorld)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    ElevatorWorld w(pCanvas, pDevice);
    RecordingListener listener;
    ASSERT_TRUE(Succeeded(w.pWorld->RegisterListener(&listener)));

    Gem::TGemPtr<Canvas::XCamera> pCamera;
    ASSERT_TRUE(Succeeded(pCanvas->CreateCamera(&pCamera, "SeatCamera")));
    ASSERT_TRUE(Succeeded(w.pSeat->BindElement(pCamera)));
    w.pWorld->SetActiveCamera(pCamera);

    // With its root node under a node of another world, the world keeps its active camera
    Gem::TGemPtr<Canvas::XWorld> pOuterWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pOuterWorld, "OuterWorld")));
    auto pHolder = MakeNode(pCanvas, pOuterWorld->GetRootNode(), "Holder", 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(Succeeded(pHolder->AddChild(w.pWorld->GetRootNode())));
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_EQ(pCamera.Get(), w.pWorld->GetActiveCamera());
    EXPECT_TRUE(listener.events.empty());

    // A camera outside the world's own scene graph is still lost, even inside the outer world
    ASSERT_TRUE(Succeeded(pHolder->BindElement(pCamera)));
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_EQ(nullptr, w.pWorld->GetActiveCamera());
    EXPECT_EQ(std::vector<std::string>({ "camera lost" }), listener.events);

    ASSERT_TRUE(Succeeded(pHolder->RemoveChild(w.pWorld->GetRootNode())));
}

namespace
{
// The names of pParent's children, in order.
std::vector<std::string> ChildNames(Canvas::XSceneNode *pParent)
{
    std::vector<std::string> names;
    for (Canvas::XSceneNode *pChild = pParent->GetFirstChild(); pChild; pChild = pParent->GetNextChild(pChild))
        names.push_back(pChild->GetName());
    return names;
}
} // anonymous namespace

TEST(CanvasInterfacesTest, SceneNodeMoves)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pA = MakeNode(pCanvas, nullptr, "A", 0.0f, 0.0f, 0.0f);
    auto pB = MakeNode(pCanvas, nullptr, "B", 0.0f, 0.0f, 0.0f);

    // A node that only its parent holds moves to a new parent intact
    {
        auto pChild = MakeNode(pCanvas, pA, "Child", 1.0f, 2.0f, 3.0f);
        auto pGrandchild = MakeNode(pCanvas, pChild, "Grandchild", 0.0f, 0.0f, 0.0f);
    }
    ASSERT_TRUE(Succeeded(pB->AddChild(pA->GetFirstChild())));
    EXPECT_EQ(nullptr, pA->GetFirstChild());
    ASSERT_NE(nullptr, pB->GetFirstChild());
    EXPECT_STREQ("Child", pB->GetFirstChild()->GetName());
    EXPECT_EQ(pB.Get(), pB->GetFirstChild()->GetParent());
    EXPECT_STREQ("Grandchild", pB->GetFirstChild()->GetFirstChild()->GetName());

    // Ordering among siblings, moving within a parent and from another
    auto pX = MakeNode(pCanvas, pB, "X", 0.0f, 0.0f, 0.0f);
    auto pY = MakeNode(pCanvas, pB, "Y", 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(std::vector<std::string>({ "Child", "X", "Y" }), ChildNames(pB));
    ASSERT_TRUE(Succeeded(pB->InsertChildBefore(pY, pX)));
    EXPECT_EQ(std::vector<std::string>({ "Child", "Y", "X" }), ChildNames(pB));
    ASSERT_TRUE(Succeeded(pB->InsertChildAfter(pX, pY)));
    EXPECT_EQ(std::vector<std::string>({ "Child", "Y", "X" }), ChildNames(pB));
    ASSERT_TRUE(Succeeded(pB->InsertChildAfter(pB->GetFirstChild(), pX)));
    EXPECT_EQ(std::vector<std::string>({ "Y", "X", "Child" }), ChildNames(pB));
    ASSERT_TRUE(Succeeded(pB->AddChild(pY)));
    EXPECT_EQ(std::vector<std::string>({ "X", "Child", "Y" }), ChildNames(pB));
    auto pZ = MakeNode(pCanvas, pA, "Z", 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(Succeeded(pB->InsertChildBefore(pZ, pX)));
    EXPECT_EQ(std::vector<std::string>({ "Z", "X", "Child", "Y" }), ChildNames(pB));
    EXPECT_EQ(nullptr, pA->GetFirstChild());

    // A failed insertion leaves the node where it was
    ASSERT_TRUE(Succeeded(pA->AddChild(pZ)));
    EXPECT_EQ(Gem::Result::InvalidArg, pA->InsertChildBefore(pX, pY));
    EXPECT_EQ(Gem::Result::InvalidArg, pB->InsertChildAfter(pX, pX));
    EXPECT_EQ(pB.Get(), pX->GetParent());
    EXPECT_EQ(std::vector<std::string>({ "X", "Child", "Y" }), ChildNames(pB));

    // A node is never added to its own subtree
    Canvas::XSceneNode *pChild = pB->GetNextChild(pX);
    ASSERT_NE(nullptr, pChild);
    EXPECT_EQ(Gem::Result::InvalidArg, pChild->GetFirstChild()->AddChild(pB));
    EXPECT_EQ(Gem::Result::InvalidArg, pChild->AddChild(pChild));
    EXPECT_EQ(Gem::Result::InvalidArg, pB->InsertChildBefore(pB, pX));
    EXPECT_EQ(nullptr, pB->GetParent());

    // A removed node's world transform is its local transform
    pB->SetLocalTransform(Translation(10.0f, 0.0f, 0.0f));
    EXPECT_TRUE(AlmostEqual(pChild->GetFirstChild()->GetGlobalTranslation(), FloatVector4(11.0f, 2.0f, 3.0f, 1.0f)));
    Gem::TGemPtr<Canvas::XSceneNode> pHeld = pChild;
    ASSERT_TRUE(Succeeded(pB->RemoveChild(pHeld)));
    EXPECT_EQ(nullptr, pHeld->GetParent());
    EXPECT_TRUE(AlmostEqual(pHeld->GetGlobalTranslation(), FloatVector4(1.0f, 2.0f, 3.0f, 1.0f)));
    EXPECT_TRUE(AlmostEqual(pHeld->GetFirstChild()->GetGlobalTranslation(), FloatVector4(1.0f, 2.0f, 3.0f, 1.0f)));
    EXPECT_EQ(Gem::Result::InvalidArg, pB->RemoveChild(pHeld));
}

TEST(CanvasInterfacesTest, SceneNodeOperationsOnWorldObjects)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    ElevatorWorld w(pCanvas, pDevice);
    RecordingListener listener;
    ASSERT_TRUE(Succeeded(w.pWorld->RegisterListener(&listener)));
    Canvas::XSceneNode *pRoot = w.pWorld->GetRootNode();

    // Removing a node detaches the world objects whose root nodes are in its subtree, which stay in the
    // world and attached to each other's nodes
    ASSERT_TRUE(Succeeded(w.pElevatorNode->RemoveChild(w.pPlatform)));
    EXPECT_EQ(w.pWorld.Get(), w.pCar->GetWorld());
    EXPECT_EQ(w.pPlatform.Get(), w.pCarNode->GetParent());
    EXPECT_EQ(w.pCar.Get(), w.pSeat->GetWorldObject());
    EXPECT_EQ(nullptr, w.pPlatform->GetWorldObject());

    // Adding it elsewhere moves them with it
    auto pGround = MakeNode(pCanvas, pRoot, "Ground", 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(Succeeded(pGround->AddChild(w.pPlatform)));
    EXPECT_EQ(w.pPlatform.Get(), w.pCarNode->GetParent());
    EXPECT_TRUE(AlmostEqual(w.pDriverNode->GetGlobalTranslation(), FloatVector4(1.0f, 6.0f, 0.5f, 1.0f)));

    // Moving a root node attaches its world object to its new parent
    ASSERT_TRUE(Succeeded(w.pElevatorNode->AddChild(w.pCarNode)));
    EXPECT_EQ(w.pElevator.Get(), w.pCarNode->GetParent()->GetWorldObject());
    ASSERT_TRUE(Succeeded(w.pElevatorNode->InsertChildBefore(w.pPlatform, w.pCarNode)));
    EXPECT_EQ(w.pElevator.Get(), w.pPlatform->GetWorldObject());

    // Removing a root node detaches its world object; the world objects attached to its nodes stay
    ASSERT_TRUE(Succeeded(w.pElevatorNode->RemoveChild(w.pCarNode)));
    EXPECT_EQ(w.pWorld.Get(), w.pCar->GetWorld());
    EXPECT_EQ(w.pSeat.Get(), w.pDriverNode->GetParent());
    ASSERT_TRUE(Succeeded(w.pWorld->PlaceObject(w.pCar, w.pPlatform, false)));
    EXPECT_EQ(w.pPlatform.Get(), w.pCarNode->GetParent());

    // No world object was orphaned
    ASSERT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
    EXPECT_TRUE(listener.events.empty());
}

TEST(CanvasInterfacesTest, WorldObjectReleaseNotPlaced)
{
    using namespace Canvas::Math;

    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    ElevatorWorld w(pCanvas, pDevice);

    // Releasing a car that is not placed leaves its orphaned driver not placed, with no parent
    ASSERT_TRUE(Succeeded(w.pWorld->DetachObject(w.pCar)));
    ASSERT_TRUE(Succeeded(w.pWorld->ReleaseWorldObject(w.pCar)));
    EXPECT_EQ(nullptr, w.pDriverNode->GetParent());
    EXPECT_EQ(w.pWorld.Get(), w.pDriver->GetWorld());
    EXPECT_EQ(w.pDriver.Get(), w.pDriverNode->GetWorldObject());

    // A world object with no root node is released like any other
    Gem::TGemPtr<Canvas::XWorldObject> pRules;
    ASSERT_TRUE(Succeeded(MakeWorldObject(w.pWorld, nullptr, &pRules)));
    ASSERT_TRUE(Succeeded(w.pWorld->ReleaseWorldObject(pRules)));
    EXPECT_EQ(nullptr, pRules->GetWorld());

    // Released and remaining world objects keep the active collection consistent
    ASSERT_TRUE(Succeeded(w.pWorld->DeactivateObject(w.pElevator)));
    ASSERT_TRUE(Succeeded(w.pWorld->ReleaseWorldObject(w.pElevator)));
    EXPECT_TRUE(Succeeded(w.pWorld->DeactivateObject(w.pDriver)));
    EXPECT_TRUE(Succeeded(w.pWorld->ActivateObject(w.pDriver)));
    EXPECT_TRUE(Succeeded(w.pWorld->Update(0.0f)));
}

TEST(CanvasInterfacesTest, WorldObjectUniqueNames)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);
    ElevatorWorld w(pCanvas, pDevice);

    // A world object has no unique name until it is given one
    EXPECT_EQ(nullptr, w.pCar->GetUniqueName());
    EXPECT_EQ(nullptr, w.pWorld->FindObject("Car"));
    EXPECT_EQ(nullptr, w.pWorld->FindObject(nullptr));

    ASSERT_TRUE(Succeeded(w.pWorld->SetUniqueName(w.pCar, "Car")));
    EXPECT_STREQ("Car", w.pCar->GetUniqueName());
    EXPECT_EQ(w.pCar.Get(), w.pWorld->FindObject("Car"));
    EXPECT_EQ(nullptr, w.pWorld->FindObject("car"));
    EXPECT_TRUE(Succeeded(w.pWorld->SetUniqueName(w.pCar, "Car")));

    // A unique name in use is not given to another world object
    EXPECT_EQ(Gem::Result::AlreadyExists, w.pWorld->SetUniqueName(w.pDriver, "Car"));
    EXPECT_EQ(nullptr, w.pDriver->GetUniqueName());
    EXPECT_EQ(w.pCar.Get(), w.pWorld->FindObject("Car"));

    // Errors
    EXPECT_EQ(Gem::Result::InvalidArg, w.pWorld->SetUniqueName(w.pCar, ""));
    EXPECT_STREQ("Car", w.pCar->GetUniqueName());
    Gem::TGemPtr<Canvas::XWorld> pOtherWorld;
    ASSERT_TRUE(Succeeded(pCanvas->CreateWorld(pDevice, &pOtherWorld, "OtherWorld")));
    EXPECT_EQ(Gem::Result::NotFound, pOtherWorld->SetUniqueName(w.pCar, "Car"));
    EXPECT_EQ(nullptr, pOtherWorld->FindObject("Car"));

    // Changing a unique name releases the old one, and nullptr clears it
    ASSERT_TRUE(Succeeded(w.pWorld->SetUniqueName(w.pCar, "Sedan")));
    EXPECT_EQ(nullptr, w.pWorld->FindObject("Car"));
    ASSERT_TRUE(Succeeded(w.pWorld->SetUniqueName(w.pDriver, "Car")));
    EXPECT_EQ(w.pDriver.Get(), w.pWorld->FindObject("Car"));
    ASSERT_TRUE(Succeeded(w.pWorld->SetUniqueName(w.pDriver, nullptr)));
    EXPECT_EQ(nullptr, w.pDriver->GetUniqueName());
    EXPECT_EQ(nullptr, w.pWorld->FindObject("Car"));

    // Names need not be unique
    Gem::TGemPtr<Canvas::XWorldObject> pSecondCar;
    ASSERT_TRUE(Succeeded(MakeWorldObject(w.pWorld, nullptr, &pSecondCar, "Car")));
    EXPECT_STREQ(w.pCar->GetName(), pSecondCar->GetName());

    // A world object that is not placed keeps its unique name; releasing one releases it
    ASSERT_TRUE(Succeeded(w.pWorld->DetachObject(w.pCar)));
    EXPECT_EQ(w.pCar.Get(), w.pWorld->FindObject("Sedan"));
    ASSERT_TRUE(Succeeded(w.pWorld->ReleaseWorldObject(w.pCar)));
    EXPECT_EQ(nullptr, w.pWorld->FindObject("Sedan"));
    EXPECT_EQ(nullptr, w.pCar->GetUniqueName());
    EXPECT_TRUE(Succeeded(w.pWorld->SetUniqueName(pSecondCar, "Sedan")));

    // A world object that outlives its world has no unique name
    w.pWorld = nullptr;
    EXPECT_EQ(nullptr, pSecondCar->GetWorld());
    EXPECT_EQ(nullptr, pSecondCar->GetUniqueName());
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

    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));
    ASSERT_NE(pInstance->GetRootNode()->GetAnimationController(), nullptr);

    Canvas::XAnimationController* pCtrl = pInstance->GetRootNode()->GetAnimationController();
    Canvas::XSceneNode*      pNode = pInstance->GetRootNode()->GetFirstChild();
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

// A bone track drives the bone in each instance's skeleton instance.  XWorld::Update runs the
// world object's controller before the elements bound to its nodes, so the socket follows the
// animated bone in the same update.
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
    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));
    ASSERT_NE(pInstance->GetRootNode()->GetAnimationController(), nullptr);

    Canvas::XSceneNode *pArmature = pInstance->GetRootNode()->GetFirstChild();
    ASSERT_NE(pArmature, nullptr);
    Canvas::XSceneNode *pHand = pArmature->GetFirstChild();
    ASSERT_NE(pHand, nullptr);
    Canvas::XSkeletonInstance *pSkeleton = FindElement<Canvas::XSkeletonInstance>(pArmature);
    ASSERT_NE(pSkeleton, nullptr);

    Canvas::XAnimationController *pCtrl = pInstance->GetRootNode()->GetAnimationController();

    EXPECT_TRUE(Succeeded(pCtrl->Play(0, 0.0f)));
    EXPECT_TRUE(Succeeded(pWorld->Update(1.0f)));

    Canvas::Transform pose;
    EXPECT_TRUE(Succeeded(pSkeleton->GetBoneLocalTransform(0, &pose)));
    EXPECT_TRUE(AlmostEqual(pose.Translation, FloatVector4(2.0f, 0.0f, 1.0f, 1.0f)));
    EXPECT_TRUE(AlmostEqual(pHand->GetLocalTranslation(), FloatVector4(2.0f, 0.0f, 1.0f, 1.0f)));

    // ResetToBindPose returns the bone to rest; the socket follows on the next update.
    pCtrl->ResetToBindPose();
    EXPECT_TRUE(Succeeded(pSkeleton->GetBoneLocalTransform(0, &pose)));
    EXPECT_TRUE(AlmostEqual(pose.Translation, FloatVector4(0.0f, 0.0f, 1.0f, 1.0f)));
    EXPECT_TRUE(Succeeded(pWorld->Update(0.0f)));
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
    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));
    ASSERT_NE(pInstance->GetRootNode()->GetAnimationController(), nullptr);
    Canvas::XAnimationController *pCtrl = pInstance->GetRootNode()->GetAnimationController();
    Canvas::XSceneNode *pNode = pInstance->GetRootNode()->GetFirstChild();
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
    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));
    ASSERT_NE(pInstance->GetRootNode()->GetAnimationController(), nullptr);
    Canvas::XAnimationController *pCtrl = pInstance->GetRootNode()->GetAnimationController();
    Canvas::XSceneNode *pNode = pInstance->GetRootNode()->GetFirstChild();
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
    Gem::TGemPtr<Canvas::XWorldObject> &pInstance)
{
    pInstance = nullptr;
    EXPECT_TRUE(Succeeded(InstantiateModel(pWorld, pModel, pWorld->GetRootNode(), &pInstance)));
    return pInstance ? pInstance->GetRootNode()->GetAnimationController() : nullptr;
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
    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    Canvas::XAnimationController *pCtrl = InstantiateAnimated(pModel.Get(), pWorld.Get(), pInstance);
    ASSERT_NE(pCtrl, nullptr);
    Canvas::XSceneNode *pNode = pInstance->GetRootNode()->GetFirstChild();
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
    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    Canvas::XAnimationController *pCtrl = InstantiateAnimated(pModel.Get(), pWorld.Get(), pInstance);
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
    Gem::TGemPtr<Canvas::XWorldObject> pInstance;
    Canvas::XAnimationController *pCtrl = InstantiateAnimated(pModel.Get(), pWorld.Get(), pInstance);
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

// The instance root of a model with clips holds the instance's own animation controller; the
// instance root of a model without clips, and a node the app builds, hold none.
TEST(CanvasInterfacesTest, SceneNodeAnimationController)
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
    Gem::TGemPtr<Canvas::XWorldObject> pFirst, pSecond, pStillInstance, pBuilt;
    Canvas::XAnimationController *pFirstCtrl = InstantiateAnimated(pAnimated.Get(), pWorld.Get(), pFirst);
    Canvas::XAnimationController *pSecondCtrl = InstantiateAnimated(pAnimated.Get(), pWorld.Get(), pSecond);
    EXPECT_NE(pFirstCtrl, nullptr);
    EXPECT_NE(pSecondCtrl, nullptr);
    EXPECT_TRUE(pFirstCtrl != pSecondCtrl);
    EXPECT_EQ(InstantiateAnimated(pStill.Get(), pWorld.Get(), pStillInstance), nullptr);
    EXPECT_NE(pStillInstance->GetRootNode(), nullptr);

    Gem::TGemPtr<Canvas::XSceneNode> pBuiltNode;
    ASSERT_TRUE(Succeeded(pCanvas->CreateSceneNode(&pBuiltNode, "Built")));
    ASSERT_TRUE(Succeeded(MakeWorldObject(pWorld, pBuiltNode, &pBuilt, "Built")));
    EXPECT_EQ(pBuiltNode->GetAnimationController(), nullptr);

    // The controller belongs to its tree: a world object updates the controller of its current
    // root node, so a swapped-out tree's animation pauses and resumes when it is swapped back
    ASSERT_TRUE(Succeeded(pFirstCtrl->Play(0, 0.0f)));
    ASSERT_TRUE(Succeeded(pWorld->Update(0.25f)));
    EXPECT_FLOAT_EQ(0.25f, pFirstCtrl->GetTime());

    Gem::TGemPtr<Canvas::XSceneNode> pAnimatedTree = pFirst->GetRootNode();
    ASSERT_TRUE(Succeeded(pBuilt->SetRootNode(nullptr)));
    ASSERT_TRUE(Succeeded(pFirst->SetRootNode(pBuiltNode)));
    EXPECT_EQ(pFirst->GetRootNode()->GetAnimationController(), nullptr);
    EXPECT_EQ(pAnimatedTree->GetAnimationController(), pFirstCtrl);
    ASSERT_TRUE(Succeeded(pWorld->Update(0.25f)));
    EXPECT_FLOAT_EQ(0.25f, pFirstCtrl->GetTime());

    ASSERT_TRUE(Succeeded(pFirst->SetRootNode(pAnimatedTree)));
    ASSERT_TRUE(Succeeded(pWorld->Update(0.25f)));
    EXPECT_FLOAT_EQ(0.5f, pFirstCtrl->GetTime());
}

}
