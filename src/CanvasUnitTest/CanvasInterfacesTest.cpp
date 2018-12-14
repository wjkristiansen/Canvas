//================================================================================================
// CanvasInterfacesTest
//================================================================================================

#include "stdafx.h"
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
            CCanvasPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(CreateCanvas(CANVAS_PPV_ARGS(&pCanvas))));

            // Create XScene object
            CCanvasPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(CANVAS_PPV_ARGS(&pScene))));

            // Create transform XSceneGraphNode
            CCanvasPtr<XTransform> pTransform;
            CCanvasPtr<XSceneGraphNode> pSceneGraphNode;
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pSceneGraphNode), "Transform")));
            Assert::IsTrue(Succeeded(pSceneGraphNode->QueryInterface(&pTransform)));

            // Create a camera XSceneGraphNode
            CCanvasPtr<XGeneric> pGeneric;
            const char szCameraName[] = "Camera";
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Camera, CANVAS_PPV_ARGS(&pGeneric), szCameraName)));

            // Make sure QI works for all the camera parts
            CCanvasPtr<XObjectName> pCameraName;
            Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCameraName)));
            CCanvasPtr<XCamera> pCamera;
            Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCamera)));
            CCanvasPtr<XTransform> pCameraTransform;
            Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCameraTransform)));
            CCanvasPtr<XTransform> pCameraSceneGraphNode;
            Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCameraSceneGraphNode)));

            // Validate the name
            Assert::IsTrue(0 == strncmp(pCameraName->GetName(), szCameraName, _countof(szCameraName)));

            // QI rules
            CCanvasPtr<XGeneric> pGeneric2;
            Assert::IsTrue(Succeeded(pCamera->QueryInterface(&pGeneric2)));
            Assert::IsTrue(pGeneric.Get() == pGeneric2.Get());
        }

        TEST_METHOD(SceneGraphNodesTest)
        {
            // Create XCanvas object
            CCanvasPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(CreateCanvas(CANVAS_PPV_ARGS(&pCanvas))));

            // Create XScene object
            CCanvasPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(CANVAS_PPV_ARGS(&pScene))));

            // Create nodes
            CCanvasPtr<XSceneGraphNode> pNodes[6];
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[0]), "Node0")));
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[1]), "Node1")));
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[2]), "Node2")));
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[3]), "Node3")));
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[4]), "Node4")));
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[5]), "Node5")));

            // Build the following tree
            // Root
            //  - Node0
            //      - Node2
            //      - Node3
            //  - Node1
            //      - Node4
            //          - Node5

            CCanvasPtr<XSceneGraphNode> pRoot;
            Assert::IsTrue(Succeeded(pScene->QueryInterface(&pRoot)));
            Assert::IsTrue(Succeeded(pRoot->AddChild(pNodes[0])));
            Assert::IsTrue(Succeeded(pRoot->AddChild(pNodes[1])));
            Assert::IsTrue(Succeeded(pNodes[0]->AddChild(pNodes[2])));
            Assert::IsTrue(Succeeded(pNodes[0]->AddChild(pNodes[3])));
            Assert::IsTrue(Succeeded(pNodes[1]->AddChild(pNodes[4])));
            Assert::IsTrue(Succeeded(pNodes[4]->AddChild(pNodes[5])));

            auto VerifyChildNode = [](XIterator *pIterator, XSceneGraphNode *pCompare)
            {
                CCanvasPtr<XSceneGraphNode> pNode;
                Assert::IsTrue(Succeeded(pIterator->Select(CANVAS_PPV_ARGS(&pNode))));
                Assert::IsTrue(pNode.Get() == pCompare);
            };

            // Verify the tree topology
            {
                CCanvasPtr<XIterator> pIterator;
                Assert::IsTrue(Succeeded(pRoot->CreateChildIterator(&pIterator)));
                VerifyChildNode(pIterator, pNodes[0]);
                Assert::IsTrue(Result::Success == pIterator->MoveNext());
                VerifyChildNode(pIterator, pNodes[1]);
                Assert::IsTrue(Result::End == pIterator->MoveNext());
            }
            {
                CCanvasPtr<XIterator> pIterator;
                Assert::IsTrue(Succeeded(pNodes[0]->CreateChildIterator(&pIterator)));
                VerifyChildNode(pIterator, pNodes[2]);
                Assert::IsTrue(Result::Success == pIterator->MoveNext());
                VerifyChildNode(pIterator, pNodes[3]);
                Assert::IsTrue(Result::End == pIterator->MoveNext());
            }
            {
                CCanvasPtr<XIterator> pIterator;
                Assert::IsTrue(Succeeded(pNodes[1]->CreateChildIterator(&pIterator)));
                VerifyChildNode(pIterator, pNodes[4]);
                Assert::IsTrue(Result::End == pIterator->MoveNext());
            }
            {
                CCanvasPtr<XIterator> pIterator;
                Assert::IsTrue(Succeeded(pNodes[4]->CreateChildIterator(&pIterator)));
                VerifyChildNode(pIterator, pNodes[5]);
                Assert::IsTrue(Result::End == pIterator->MoveNext());
            }
        }
    };
}