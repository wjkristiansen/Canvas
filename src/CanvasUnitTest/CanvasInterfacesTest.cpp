//================================================================================================
// CanvasInterfacesTest
//================================================================================================

#include "pch.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Canvas;

namespace CanvasUnitTest
{
    TEST_CLASS(CanvasInterfacesTest)
    {
    public:

        TEST_METHOD(SimpleInterfaces)
        {
            // Create XCanvas object
            Gem::TGemPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(Canvas::CreateCanvas(&pCanvas)));

            // Create XScene object
            Gem::TGemPtr<Canvas::XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(&pScene)));

            // Create an empty XSceneGraphNode
            Gem::TGemPtr<Canvas::XSceneGraphNode> pSceneGraphNode;
            const char szNodeName[] = "NullNode";
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pSceneGraphNode)));
            pSceneGraphNode->SetName(szNodeName);

            // Verify QI for XGeneric
            Gem::TGemPtr<XGeneric> pGeneric;
            Assert::IsTrue(Succeeded(pSceneGraphNode->QueryInterface(&pGeneric)));

            // Validate the name
            Assert::IsTrue(0 == strncmp(pSceneGraphNode->GetName(), szNodeName, _countof(szNodeName)));
        }

        TEST_METHOD(SceneGraphNodesTest)
        {
            // Create XCanvas object
            Gem::TGemPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(Canvas::CreateCanvas(&pCanvas)));

            // Create XScene object
            Gem::TGemPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(&pScene)));

            // Create nodes
            Gem::TGemPtr<XSceneGraphNode> pNodes[6];
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[0])));
            pNodes[0]->SetName("Node0");
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[1])));
            pNodes[1]->SetName("Node1");
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[2])));
            pNodes[2]->SetName("Node2");
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[3])));
            pNodes[3]->SetName("Node3");
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[4])));
            pNodes[4]->SetName("Node4");
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[5])));
            pNodes[5]->SetName("Node5");

            // Build the following tree
            // Root
            //  - Node0
            //      - Node2
            //      - Node3
            //  - Node1
            //      - Node4
            //          - Node5

            Gem::TGemPtr<XSceneGraphNode> pRoot = pScene->GetRootSceneGraphNode();
            Assert::IsNotNull(pRoot.Get());
            Assert::IsTrue(Succeeded(pRoot->AddChild(pNodes[0])));
            Assert::IsTrue(Succeeded(pRoot->AddChild(pNodes[1])));
            Assert::IsTrue(Succeeded(pNodes[0]->AddChild(pNodes[2])));
            Assert::IsTrue(Succeeded(pNodes[0]->AddChild(pNodes[3])));
            Assert::IsTrue(Succeeded(pNodes[1]->AddChild(pNodes[4])));
            Assert::IsTrue(Succeeded(pNodes[4]->AddChild(pNodes[5])));
        }
    };
}