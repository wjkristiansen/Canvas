//================================================================================================
// SceneChangeTrackerTest -- a tracker reports each tracked node whose world-space extent may
// have changed, once per ClearChanges, and nothing else.
//================================================================================================

#include "pch.h"
#include "SceneChangeTracker.h"

#include <algorithm>

using namespace Canvas;
using namespace Canvas::Math;

namespace CanvasUnitTest
{

namespace
{

Gem::TGemPtr<XSceneNode> MakeChild(XCanvas* canvas, XSceneNode* parent)
{
    Gem::TGemPtr<XSceneNode> pNode;
    EXPECT_TRUE(Succeeded(canvas->CreateSceneNode(&pNode)));
    if (parent)
        EXPECT_TRUE(Succeeded(parent->AddChild(pNode)));
    return pNode;
}

size_t ReportCount(const SceneChangeTracker& tracker, XSceneNode* node)
{
    const auto& changed = tracker.GetChangedNodes();
    return static_cast<size_t>(std::count_if(changed.begin(), changed.end(),
        [node](XSceneNode* p) { return p == node; }));
}

const FloatVector4 kOffset(1.0f, 2.0f, 3.0f, 1.0f);

} // anonymous namespace

// A tracked node is reported once however often it moves before ClearChanges, and an untracked
// node is never reported.
TEST(SceneChangeTrackerTest, ReportsTrackedNodesOnce)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pTracked = MakeChild(pCanvas, nullptr);
    auto pUntracked = MakeChild(pCanvas, nullptr);

    SceneChangeTracker tracker;
    tracker.Track(pTracked);
    EXPECT_TRUE(tracker.GetChangedNodes().empty());

    pTracked->SetLocalTranslation(kOffset);
    pTracked->SetLocalScale(FloatVector4(2.0f, 2.0f, 2.0f, 0.0f));
    pUntracked->SetLocalTranslation(kOffset);
    EXPECT_EQ(size_t(1), ReportCount(tracker, pTracked));
    EXPECT_EQ(size_t(0), ReportCount(tracker, pUntracked));

    tracker.ClearChanges();
    EXPECT_TRUE(tracker.GetChangedNodes().empty());

    pTracked->SetLocalTranslation(kOffset);
    EXPECT_EQ(size_t(1), ReportCount(tracker, pTracked));

    tracker.Untrack(pTracked);
    EXPECT_TRUE(tracker.GetChangedNodes().empty());
}

// Moving an ancestor reports tracked descendants, including after a ClearChanges that followed
// an earlier ancestor move nobody read the descendants' matrices for.
TEST(SceneChangeTrackerTest, ReportsDescendantsOfMovedAncestors)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pGrandparent = MakeChild(pCanvas, nullptr);
    auto pParent = MakeChild(pCanvas, pGrandparent);
    auto pChild = MakeChild(pCanvas, pParent);

    SceneChangeTracker tracker;
    pChild->GetGlobalMatrix();
    tracker.Track(pChild);

    pParent->SetLocalTranslation(kOffset);
    EXPECT_EQ(size_t(1), ReportCount(tracker, pChild));
    tracker.ClearChanges();

    pGrandparent->SetLocalTranslation(kOffset);
    EXPECT_EQ(size_t(1), ReportCount(tracker, pChild));
    tracker.ClearChanges();

    pGrandparent->SetLocalTranslation(FloatVector4(0.0f, 0.0f, 0.0f, 1.0f));
    pParent->SetLocalTranslation(FloatVector4(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(size_t(1), ReportCount(tracker, pChild));

    tracker.Untrack(pChild);
}

// Binding an element to a tracked node, and changing an element's bounds, report the node.
TEST(SceneChangeTrackerTest, ReportsElementChanges)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pNode = MakeChild(pCanvas, nullptr);
    Gem::TGemPtr<XLight> pLight;
    EXPECT_TRUE(Succeeded(pCanvas->CreateLight(LightType::Spot, &pLight)));
    Gem::TGemPtr<XMeshInstance> pMesh;
    EXPECT_TRUE(Succeeded(pCanvas->CreateMeshInstance(&pMesh)));

    SceneChangeTracker tracker;
    pNode->GetGlobalMatrix();
    tracker.Track(pNode);

    EXPECT_TRUE(Succeeded(pNode->BindElement(pLight)));
    EXPECT_EQ(size_t(1), ReportCount(tracker, pNode)) << "BindElement";
    tracker.ClearChanges();

    pLight->SetRange(5.0f);
    EXPECT_EQ(size_t(1), ReportCount(tracker, pNode)) << "SetRange";
    tracker.ClearChanges();

    pLight->SetSpotAngles(0.2f, 0.4f);
    EXPECT_EQ(size_t(1), ReportCount(tracker, pNode)) << "SetSpotAngles";
    tracker.ClearChanges();

    pLight->SetIntensity(3.0f);
    EXPECT_EQ(size_t(0), ReportCount(tracker, pNode)) << "SetIntensity changes no extent";

    EXPECT_TRUE(Succeeded(pNode->BindElement(pMesh)));
    tracker.ClearChanges();
    pMesh->SetMeshData(nullptr);
    EXPECT_EQ(size_t(1), ReportCount(tracker, pNode)) << "SetMeshData";

    tracker.Untrack(pNode);
}

// Track and Untrack nest: a node stays tracked until every Track is matched.
TEST(SceneChangeTrackerTest, TrackNests)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pNode = MakeChild(pCanvas, nullptr);

    SceneChangeTracker tracker;
    pNode->GetGlobalMatrix();
    tracker.Track(pNode);
    tracker.Track(pNode);

    tracker.Untrack(pNode);
    pNode->SetLocalTranslation(kOffset);
    EXPECT_EQ(size_t(1), ReportCount(tracker, pNode));

    tracker.Untrack(pNode);
    EXPECT_TRUE(tracker.GetChangedNodes().empty());
    pNode->SetLocalTranslation(FloatVector4(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(tracker.GetChangedNodes().empty());
}

// Adding or removing a node anywhere under the watched root, or binding an element to one,
// reports a structure change.  Reordering children, moving nodes, and changes to a graph the
// root does not contain do not.
TEST(SceneChangeTrackerTest, ReportsStructureChanges)
{
    Gem::TGemPtr<XCanvas> pCanvas;
    Gem::TGemPtr<XGfxDevice> pDevice;
    CreateTestCanvasAndDevice(pCanvas, pDevice);

    auto pRoot = MakeChild(pCanvas, nullptr);
    auto pBranch = MakeChild(pCanvas, pRoot);
    auto pLeafA = MakeChild(pCanvas, pBranch);
    auto pLeafB = MakeChild(pCanvas, pBranch);

    // A separate graph, built up before it joins the watched one.
    auto pSubtree = MakeChild(pCanvas, nullptr);
    auto pSubtreeChild = MakeChild(pCanvas, pSubtree);

    SceneChangeTracker tracker;
    tracker.WatchStructure(pRoot);
    EXPECT_FALSE(tracker.HasStructureChanged());

    Gem::TGemPtr<XLight> pLight;
    EXPECT_TRUE(Succeeded(pCanvas->CreateLight(LightType::Point, &pLight)));
    EXPECT_TRUE(Succeeded(pSubtreeChild->BindElement(pLight)));
    pLeafA->SetLocalTranslation(kOffset);
    EXPECT_TRUE(Succeeded(pBranch->InsertChildBefore(pLeafB, pLeafA)));
    EXPECT_FALSE(tracker.HasStructureChanged()) << "Unwatched graph, a move, and a reorder";

    EXPECT_TRUE(Succeeded(pLeafA->AddChild(pSubtree)));
    EXPECT_TRUE(tracker.HasStructureChanged()) << "AddChild";
    tracker.ClearChanges();
    EXPECT_FALSE(tracker.HasStructureChanged());

    Gem::TGemPtr<XMeshInstance> pMesh;
    EXPECT_TRUE(Succeeded(pCanvas->CreateMeshInstance(&pMesh)));
    EXPECT_TRUE(Succeeded(pSubtreeChild->BindElement(pMesh)));
    EXPECT_TRUE(tracker.HasStructureChanged()) << "BindElement deep under the root";
    tracker.ClearChanges();

    EXPECT_TRUE(Succeeded(pLeafB->AddChild(pSubtreeChild)));
    EXPECT_TRUE(tracker.HasStructureChanged()) << "Reparenting within the graph";
    tracker.ClearChanges();

    EXPECT_TRUE(Succeeded(pBranch->RemoveChild(pLeafA)));
    EXPECT_TRUE(tracker.HasStructureChanged()) << "RemoveChild";
    tracker.ClearChanges();

    EXPECT_TRUE(Succeeded(pLeafA->AddChild(MakeChild(pCanvas, nullptr))));
    EXPECT_FALSE(tracker.HasStructureChanged()) << "Under a node removed from the graph";
}

} // namespace CanvasUnitTest
