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
            TCanvasPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(CreateCanvas(CANVAS_PPV_ARGS(&pCanvas))));

            // Create XScene object
            TCanvasPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(CANVAS_PPV_ARGS(&pScene))));

            // Create transform XSceneGraphNode
            TCanvasPtr<XTransform> pTransform;
            TCanvasPtr<XSceneGraphNode> pSceneGraphNode;
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pSceneGraphNode), L"Transform")));
            Assert::IsTrue(Succeeded(pSceneGraphNode->QueryInterface(&pTransform)));

            // Create a camera XSceneGraphNode
            TCanvasPtr<XGeneric> pGeneric;
            const wchar_t szCameraName[] = L"Camera";
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Camera, CANVAS_PPV_ARGS(&pGeneric), szCameraName)));

            // Make sure QI works for all the camera parts
            TCanvasPtr<XObjectName> pCameraName;
            Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCameraName)));
            TCanvasPtr<XCamera> pCamera;
            Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCamera)));
            TCanvasPtr<XTransform> pCameraTransform;
            Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCameraTransform)));
            TCanvasPtr<XTransform> pCameraSceneGraphNode;
            Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCameraSceneGraphNode)));

            // Validate the name
            Assert::IsTrue(0 == wcsncmp(pCameraName->GetName(), szCameraName, _countof(szCameraName)));

            // QI rules
            TCanvasPtr<XGeneric> pGeneric2;
            Assert::IsTrue(Succeeded(pCamera->QueryInterface(&pGeneric2)));
            Assert::IsTrue(pGeneric.Get() == pGeneric2.Get());
        }

        TEST_METHOD(SceneGraphNodesTest)
        {
            // Create XCanvas object
            TCanvasPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(CreateCanvas(CANVAS_PPV_ARGS(&pCanvas))));

            // Create XScene object
            TCanvasPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(CANVAS_PPV_ARGS(&pScene))));

            // Create nodes
            TCanvasPtr<XSceneGraphNode> pNodes[6];
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[0]), L"Node0")));
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[1]), L"Node1")));
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[2]), L"Node2")));
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[3]), L"Node3")));
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[4]), L"Node4")));
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pNodes[5]), L"Node5")));

            // Build the following tree
            // Root
            //  - Node0
            //      - Node2
            //      - Node3
            //  - Node1
            //      - Node4
            //          - Node5

            TCanvasPtr<XSceneGraphNode> pRoot;
            Assert::IsTrue(Succeeded(pScene->QueryInterface(&pRoot)));
            Assert::IsTrue(Succeeded(pRoot->AddChild(pNodes[0])));
            Assert::IsTrue(Succeeded(pRoot->AddChild(pNodes[1])));
            Assert::IsTrue(Succeeded(pNodes[0]->AddChild(pNodes[2])));
            Assert::IsTrue(Succeeded(pNodes[0]->AddChild(pNodes[3])));
            Assert::IsTrue(Succeeded(pNodes[1]->AddChild(pNodes[4])));
            Assert::IsTrue(Succeeded(pNodes[4]->AddChild(pNodes[5])));

            auto VerifyChildNode = [](XIterator *pIterator, XSceneGraphNode *pCompare)
            {
                TCanvasPtr<XSceneGraphNode> pNode;
                Assert::IsTrue(Succeeded(pIterator->Select(CANVAS_PPV_ARGS(&pNode))));
                Assert::IsTrue(pNode.Get() == pCompare);
            };

            // Verify the tree topology
            {
                TCanvasPtr<XIterator> pIterator;
                Assert::IsTrue(Succeeded(pRoot->CreateChildIterator(&pIterator)));
                VerifyChildNode(pIterator, pNodes[0]);
                Assert::IsTrue(Result::Success == pIterator->MoveNext());
                VerifyChildNode(pIterator, pNodes[1]);
                Assert::IsTrue(Result::End == pIterator->MoveNext());
                Assert::IsTrue(pIterator->IsAtEnd());
                Assert::IsTrue(Result::Success == pIterator->MovePrev());
                Assert::IsTrue(Result::Success == pIterator->MovePrev());
                VerifyChildNode(pIterator, pNodes[0]);
                Assert::IsTrue(Result::End == pIterator->MovePrev());
            }
            {
                TCanvasPtr<XIterator> pIterator;
                Assert::IsTrue(Succeeded(pNodes[0]->CreateChildIterator(&pIterator)));
                VerifyChildNode(pIterator, pNodes[2]);
                Assert::IsTrue(Result::Success == pIterator->MoveNext());
                VerifyChildNode(pIterator, pNodes[3]);
                Assert::IsTrue(Result::End == pIterator->MoveNext());
            }
            {
                TCanvasPtr<XIterator> pIterator;
                Assert::IsTrue(Succeeded(pNodes[1]->CreateChildIterator(&pIterator)));
                VerifyChildNode(pIterator, pNodes[4]);
                Assert::IsTrue(Result::End == pIterator->MoveNext());
            }
            {
                TCanvasPtr<XIterator> pIterator;
                Assert::IsTrue(Succeeded(pNodes[4]->CreateChildIterator(&pIterator)));
                VerifyChildNode(pIterator, pNodes[5]);
                Assert::IsTrue(Result::End == pIterator->MoveNext());
            }
        }
    };
}