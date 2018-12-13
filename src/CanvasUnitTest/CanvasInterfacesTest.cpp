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
    };
}