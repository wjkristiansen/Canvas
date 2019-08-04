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
            TGemPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(CreateCanvas(GEM_IID_PPV_ARGS(&pCanvas))));

            // Create XScene object
            TGemPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(GEM_IID_PPV_ARGS(&pScene))));

            // Create transform XSceneGraphNode
            TGemPtr<XTransform> pTransform;
            TGemPtr<XSceneGraphNode> pSceneGraphNode;
            Assert::IsTrue(Succeeded(pCanvas->CreateNullSceneGraphNode(GEM_IID_PPV_ARGS(&pSceneGraphNode), L"Transform")));
            Assert::IsTrue(Succeeded(pSceneGraphNode->QueryInterface(&pTransform)));

            // Create a camera XSceneGraphNode
            TGemPtr<XGeneric> pGeneric;
            const wchar_t szCameraName[] = L"Camera";
            Assert::IsTrue(Succeeded(pCanvas->CreateNullSceneGraphNode(GEM_IID_PPV_ARGS(&pGeneric), szCameraName)));

            // Make sure QI works for all the camera parts
            TGemPtr<XNameTag> pCameraName;
            Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCameraName)));
            //TGemPtr<XCamera> pCamera;
            //Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCamera)));
            TGemPtr<XTransform> pCameraTransform;
            Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCameraTransform)));
            TGemPtr<XTransform> pCameraSceneGraphNode;
            Assert::IsTrue(Succeeded(pGeneric->QueryInterface(&pCameraSceneGraphNode)));

            // Validate the name
            Assert::IsTrue(0 == wcsncmp(pCameraName->GetName(), szCameraName, _countof(szCameraName)));

            // QI rules
            TGemPtr<XGeneric> pGeneric2;
            Assert::IsTrue(Succeeded(pCameraTransform->QueryInterface(&pGeneric2)));
            Assert::IsTrue(pGeneric.Get() == pGeneric2.Get());
        }

        TEST_METHOD(SceneGraphNodesTest)
        {
            // Create XCanvas object
            TGemPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(CreateCanvas(GEM_IID_PPV_ARGS(&pCanvas))));

            // Create XScene object
            TGemPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(GEM_IID_PPV_ARGS(&pScene))));

            // Create nodes
            TGemPtr<XSceneGraphNode> pNodes[6];
            Assert::IsTrue(Succeeded(pCanvas->CreateNullSceneGraphNode(GEM_IID_PPV_ARGS(&pNodes[0]), L"Node0")));
            Assert::IsTrue(Succeeded(pCanvas->CreateNullSceneGraphNode(GEM_IID_PPV_ARGS(&pNodes[1]), L"Node1")));
            Assert::IsTrue(Succeeded(pCanvas->CreateNullSceneGraphNode(GEM_IID_PPV_ARGS(&pNodes[2]), L"Node2")));
            Assert::IsTrue(Succeeded(pCanvas->CreateNullSceneGraphNode(GEM_IID_PPV_ARGS(&pNodes[3]), L"Node3")));
            Assert::IsTrue(Succeeded(pCanvas->CreateNullSceneGraphNode(GEM_IID_PPV_ARGS(&pNodes[4]), L"Node4")));
            Assert::IsTrue(Succeeded(pCanvas->CreateNullSceneGraphNode(GEM_IID_PPV_ARGS(&pNodes[5]), L"Node5")));

            // Build the following tree
            // Root
            //  - Node0
            //      - Node2
            //      - Node3
            //  - Node1
            //      - Node4
            //          - Node5

            TGemPtr<XSceneGraphNode> pRoot;
            Assert::IsTrue(Succeeded(pScene->QueryInterface(GEM_IID_PPV_ARGS(&pRoot))));
            Assert::IsTrue(Succeeded(pRoot->AddChild(pNodes[0])));
            Assert::IsTrue(Succeeded(pRoot->AddChild(pNodes[1])));
            Assert::IsTrue(Succeeded(pNodes[0]->AddChild(pNodes[2])));
            Assert::IsTrue(Succeeded(pNodes[0]->AddChild(pNodes[3])));
            Assert::IsTrue(Succeeded(pNodes[1]->AddChild(pNodes[4])));
            Assert::IsTrue(Succeeded(pNodes[4]->AddChild(pNodes[5])));

            auto VerifyChildNode = [](XIterator *pIterator, XSceneGraphNode *pCompare)
            {
                TGemPtr<XSceneGraphNode> pNode;
                Assert::IsTrue(Succeeded(pIterator->Select(GEM_IID_PPV_ARGS(&pNode))));
                Assert::IsTrue(pNode.Get() == pCompare);
            };

            // Verify the tree topology
            {
                TGemPtr<XIterator> pIterator;
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
                TGemPtr<XIterator> pIterator;
                Assert::IsTrue(Succeeded(pNodes[0]->CreateChildIterator(&pIterator)));
                VerifyChildNode(pIterator, pNodes[2]);
                Assert::IsTrue(Result::Success == pIterator->MoveNext());
                VerifyChildNode(pIterator, pNodes[3]);
                Assert::IsTrue(Result::End == pIterator->MoveNext());
            }
            {
                TGemPtr<XIterator> pIterator;
                Assert::IsTrue(Succeeded(pNodes[1]->CreateChildIterator(&pIterator)));
                VerifyChildNode(pIterator, pNodes[4]);
                Assert::IsTrue(Result::End == pIterator->MoveNext());
            }
            {
                TGemPtr<XIterator> pIterator;
                Assert::IsTrue(Succeeded(pNodes[4]->CreateChildIterator(&pIterator)));
                VerifyChildNode(pIterator, pNodes[5]);
                Assert::IsTrue(Result::End == pIterator->MoveNext());
            }
        }
    };
}