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
            Assert::IsTrue(Succeeded(pCanvas->CreateObject(ObjectType::Scene, CANVAS_PPV_ARGS(&pScene))));

            // Create transform XSceneGraphNode
            CCanvasPtr<XTransform> pTransformElement;
            CCanvasPtr<XSceneGraphNode> pSceneGraphNode;
            //Assert::IsTrue(Succeeded(pCanvas->CreateNode("Transform", OBJECT_ELEMENT_FLAG_TRANSFORM, CANVAS_PPV_ARGS(&pSceneGraphNode))));
            //Assert::IsTrue(Succeeded(pSceneGraphNode->QueryInterface(&pTransformElement)));

            // Create a camera XSceneGraphNode
            //CCanvasPtr<XGeneric> pCameraElement;
            //Assert::IsTrue(Succeeded(pCanvas->CreateNode("Camera", OBJECT_ELEMENT_FLAG_CAMERA, CANVAS_PPV_ARGS(&pCameraElement))));

            //// QI rules
            //CCanvasPtr<XCamera> pCamera2;
            //Assert::IsTrue(Succeeded(pCameraElement->QueryInterface(&pCamera2)));
            //CCanvasPtr<XGeneric> pGeneric;
            //Assert::IsTrue(Succeeded(pCamera2->QueryInterface(&pGeneric)));
            //Assert::IsTrue(pGeneric.p == pCameraElement.p);
        }
    };
}