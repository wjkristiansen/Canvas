// CanvasConsole.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>

float DoVectorThing(float v[2])
{
    return v[0] + v[1];
}

void DumpNode(Canvas::XSceneGraphNode *pNode, const std::string &indent)
{
    PCSTR szName = pNode->GetName();
    if (szName)
    {
        std::cout << indent << szName << std::endl;
    }
    else
    {
        std::cout << indent << "<unnamed node>" << std::endl;
    }

    // Dump children
    for (Canvas::XSceneGraphNode *pChild = pNode->GetFirstChild(); pChild; pChild = pChild->GetSibling())
    {
        DumpNode(pChild, indent + "  ");
    }
}

int main()
{
    Canvas::Math::FloatVector2 fv2a(1.0f, 2.0f);
    Canvas::Math::FloatVector2 fv2b(3.0f, 3.0f);  
    Canvas::Math::FloatVector2 fv2c = fv2a + fv2b;

    Canvas::Math::DoubleVector3 dv3a(2.2, 3.3, 4.4);
    Canvas::Math::DoubleVector3 dv3b(3.0, 4.0, 5.0);
    Canvas::Math::DoubleVector3 dv3c = dv3b - dv3a;

    float X = fv2c[0];
    float Y = fv2c[1];
    float Dot = DotProduct(fv2a, fv2b);
    auto Cross = CrossProduct(dv3a, dv3b);

    Canvas::Math::TMatrix<float, 4, 4> fm44a(
        Canvas::Math::FloatVector4(1.1f, 1.2f, 1.3f, 1.4f),
        Canvas::Math::FloatVector4(2.1f, 2.2f, 2.3f, 2.4f),
        Canvas::Math::FloatVector4(3.1f, 3.2f, 3.3f, 3.4f),
        Canvas::Math::FloatVector4(4.1f, 4.2f, 4.3f, 4.4f)
    );
    Canvas::Math::TMatrix<float, 4, 4> fm44b(
        Canvas::Math::FloatVector4(10.1f, 10.2f, 10.3f, 10.4f),
        Canvas::Math::FloatVector4(20.1f, 20.2f, 20.3f, 20.4f),
        Canvas::Math::FloatVector4(30.1f, 30.2f, 30.3f, 30.4f),
        Canvas::Math::FloatVector4(40.1f, 40.2f, 40.3f, 40.4f)
    );
    Canvas::Math::TVector<float, 4> fv4a(3.1f, 4.2f, 5.3f, 6.4f);
    auto fv4b = fv4a * fm44a;

    auto fm44c = fm44a * fm44b;

    Gem::TGemPtr<Canvas::XCanvas> pCanvas;
    Gem::Result result = Canvas::CreateCanvas(&pCanvas);

    Gem::TGemPtr<Canvas::XScene> pScene;
    result = pCanvas->CreateScene(&pScene);

    Gem::TGemPtr<Canvas::XSceneGraphNode> pRootSceneGraphNode;
    result = pScene->QueryInterface(&pRootSceneGraphNode);

    DumpNode(pRootSceneGraphNode, "");
}

