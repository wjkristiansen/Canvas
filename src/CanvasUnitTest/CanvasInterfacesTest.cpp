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
            // Create ICanvas object
            CComPtr<ICanvas> pCanvas;
            Assert::IsTrue(Succeeded(CreateCanvas(CANVAS_IID_PPV_ARGS(&pCanvas))));

            // Create IScene object
            CComPtr<IScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(CANVAS_IID_PPV_ARGS(&pScene))));

            // Create transform ISceneGraphNode
            CComPtr<ITransform> pTransformElement;
            CComPtr<ISceneGraphNode> pSceneGraphNode;
            Assert::IsTrue(Succeeded(pCanvas->CreateNode("Transform", NODE_ELEMENT_FLAGS_TRANSFORM, CANVAS_IID_PPV_ARGS(&pSceneGraphNode))));
            Assert::IsTrue(Succeeded(pSceneGraphNode->QueryInterface(&pTransformElement)));

            // Create a camera ISceneGraphNode
            CComPtr<IGeneric> pCameraElement;
            Assert::IsTrue(Succeeded(pCanvas->CreateNode("Camera", NODE_ELEMENT_FLAGS_CAMERA, CANVAS_IID_PPV_ARGS(&pCameraElement))));

            // QI rules
            CComPtr<ICamera> pCamera2;
            Assert::IsTrue(Succeeded(pCameraElement->QueryInterface(&pCamera2)));
            CComPtr<IGeneric> pGeneric;
            Assert::IsTrue(Succeeded(pCamera2->QueryInterface(&pGeneric)));
            Assert::IsTrue(pGeneric.p == pCameraElement.p);
        }
    };
}