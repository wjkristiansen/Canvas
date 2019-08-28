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
    TGemPtr<XNameTag> pNameTag;
    if (Succeeded(pNode->QueryInterface(GEM_IID_PPV_ARGS(&pNameTag))))
    {
        std::cout << indent << pNameTag->GetName() << std::endl;
    }
    else
    {
        std::cout << indent << "<unnamed node>" << std::endl;
    }

    // Dump children
    TGemPtr<XIterator> pChildIterator;
    if (Succeeded(pNode->CreateChildIterator(&pChildIterator)))
    {
        for (pChildIterator->Reset(); !pChildIterator->IsAtEnd(); pChildIterator->MoveNext())
        {
            TGemPtr<XSceneGraphNode> pChildNode;
            pChildIterator->Select(GEM_IID_PPV_ARGS(&pChildNode));
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

    float X = fv2c[0];
    float Y = fv2c[1];
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

    TGemPtr<XCanvas> pCanvas;
    Result result = CreateCanvas(XCanvas::IId, (void **)&pCanvas);

    TGemPtr<XScene> pScene;
    result = pCanvas->CreateScene(XScene::IId, (void **)&pScene);

    TGemPtr<XSceneGraphNode> pRootSceneGraphNode;
    result = pScene->QueryInterface(&pRootSceneGraphNode);

    DumpNode(pRootSceneGraphNode, "");

    TGemPtr<XGeneric> pGeneric1;
    TGemPtr<XGeneric> pGeneric2;
    TGemPtr<XTransform> pTransform;
    //result = pCanvas->CreateNode("My Transform", Canvas::OBJECT_ELEMENT_FLAG_SCENE_GRAPH_NODE|Canvas::OBJECT_ELEMENT_FLAG_TRANSFORM, XTransform::IId, reinterpret_cast<void **>(&pTransform));
    //TGemPtr<XSceneGraphNode> pNode;
    //pTransform->QueryInterface(&pNode);
    //pTransform->QueryInterface(&pGeneric1);
    //pNode->QueryInterface(&pGeneric2);
}

