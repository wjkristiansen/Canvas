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
            CComPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(CreateCanvas(CANVAS_IID_PPV_ARGS(&pCanvas))));

            // Create XScene object
            CComPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(CANVAS_IID_PPV_ARGS(&pScene))));

            // Create transform XSceneGraphNode
            CComPtr<XTransform> pTransformElement;
            CComPtr<XSceneGraphNode> pSceneGraphNode;
            Assert::IsTrue(Succeeded(pCanvas->CreateNode("Transform", OBJECT_ELEMENT_FLAG_TRANSFORM, CANVAS_IID_PPV_ARGS(&pSceneGraphNode))));
            Assert::IsTrue(Succeeded(pSceneGraphNode->QueryInterface(&pTransformElement)));

            // Create a camera XSceneGraphNode
            CComPtr<XGeneric> pCameraElement;
            Assert::IsTrue(Succeeded(pCanvas->CreateNode("Camera", OBJECT_ELEMENT_FLAG_CAMERA, CANVAS_IID_PPV_ARGS(&pCameraElement))));

            // QI rules
            CComPtr<XCamera> pCamera2;
            Assert::IsTrue(Succeeded(pCameraElement->QueryInterface(&pCamera2)));
            CComPtr<XGeneric> pGeneric;
            Assert::IsTrue(Succeeded(pCamera2->QueryInterface(&pGeneric)));
            Assert::IsTrue(pGeneric.p == pCameraElement.p);
        }
    };
}