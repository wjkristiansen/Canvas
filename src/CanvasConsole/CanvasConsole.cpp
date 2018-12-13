// CanvasConsole.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>

using namespace Canvas;

float DoVectorThing(float v[2])
{
    return v[0] + v[1];
}

void DumpNode(XSceneGraphNode *pNode, const std::string &indent)
{
    CCanvasPtr<XObjectName> pName;
    if (Succeeded(pNode->QueryInterface(&pName)))
    {
        std::cout << indent << pName->GetName() << std::endl;
    }
    else
    {
        std::cout << indent << "<unnamed node>" << std::endl;
    }

    // Dump children
    CCanvasPtr<XIterator> pChildIterator;
    if (Succeeded(pNode->CreateChildIterator(&pChildIterator)))
    {
        for (pChildIterator->Reset(); !pChildIterator->IsAtEnd(); pChildIterator->MoveNext())
        {
            CCanvasPtr<XSceneGraphNode> pChildNode;
            pChildIterator->Select(CANVAS_PPV_ARGS(&pChildNode));
            DumpNode(pChildNode, indent + "  ");
        }
    }
}

int main()
{
    FloatVector2 fv2a(1.0f, 2.0f);
    FloatVector2 fv2b(3.0f, 3.0f);  
    FloatVector2 fv2c = fv2a + fv2b;

    DoubleVector3 dv3a(2.2, 3.3, 4.4);
    DoubleVector3 dv3b(3.0, 4.0, 5.0);
    DoubleVector3 dv3c = dv3b - dv3a;

    float X = fv2c.X();
    float Y = fv2c.Y();
    float Dot = DotProduct(fv2a, fv2b);
    auto Cross = CrossProduct(dv3a, dv3b);

    TMatrix<float, 4, 4> fm44a(
        FloatVector4(1.1f, 1.2f, 1.3f, 1.4f),
        FloatVector4(2.1f, 2.2f, 2.3f, 2.4f),
        FloatVector4(3.1f, 3.2f, 3.3f, 3.4f),
        FloatVector4(4.1f, 4.2f, 4.3f, 4.4f)
    );
    TMatrix<float, 4, 4> fm44b(
        FloatVector4(10.1f, 10.2f, 10.3f, 10.4f),
        FloatVector4(20.1f, 20.2f, 20.3f, 20.4f),
        FloatVector4(30.1f, 30.2f, 30.3f, 30.4f),
        FloatVector4(40.1f, 40.2f, 40.3f, 40.4f)
    );
    TVector<float, 4> fv4a(3.1f, 4.2f, 5.3f, 6.4f);
    auto fv4b = fv4a * fm44a;

    auto fm44c = fm44a * fm44b;

    CCanvasPtr<XCanvas> pCanvas;
    Canvas::Result result = CreateCanvas(XCanvas::IId, (void **)&pCanvas);

    CCanvasPtr<XScene> pScene;
    result = pCanvas->CreateObject(ObjectType::Scene, XScene::IId, (void **)&pScene);

    CCanvasPtr<XSceneGraphNode> pRootSceneGraphNode;
    result = pScene->QueryInterface(&pRootSceneGraphNode);

    CCanvasPtr<XSceneGraphNode> pTransformNode1;
    result = pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pTransformNode1), "TransformNode1");

    CCanvasPtr<XSceneGraphNode> pTransformNode2;
    result = pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pTransformNode2), "TransformNode2");

    CCanvasPtr<XSceneGraphNode> pTransformNode3;
    result = pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pTransformNode3), "TransformNode3");

    CCanvasPtr<XSceneGraphNode> pTransformNode4;
    result = pCanvas->CreateObject(ObjectType::Transform, CANVAS_PPV_ARGS(&pTransformNode4), "TransformNode4");

    pRootSceneGraphNode->AddChild(pTransformNode1);
    pTransformNode1->AddChild(pTransformNode2);
    pTransformNode1->AddChild(pTransformNode3);
    pRootSceneGraphNode->AddChild(pTransformNode4);
    pRootSceneGraphNode->AddRef(); // BUGBUG - DELIBERATE LEAK FOR SANITY TESTING LEAK REPORTING

    DumpNode(pRootSceneGraphNode, "");

    CCanvasPtr<XGeneric> pGeneric1;
    CCanvasPtr<XGeneric> pGeneric2;
    CCanvasPtr<XTransform> pTransform;
    //result = pCanvas->CreateNode("My Transform", Canvas::OBJECT_ELEMENT_FLAG_SCENE_GRAPH_NODE|Canvas::OBJECT_ELEMENT_FLAG_TRANSFORM, XTransform::IId, reinterpret_cast<void **>(&pTransform));
    //CCanvasPtr<XSceneGraphNode> pNode;
    //pTransform->QueryInterface(&pNode);
    //pTransform->QueryInterface(&pGeneric1);
    //pNode->QueryInterface(&pGeneric2);
}

