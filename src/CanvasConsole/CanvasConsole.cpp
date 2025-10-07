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
    Gem::TGemPtr<Canvas::XCanvas> pCanvas;
    Gem::Result result = Canvas::CreateCanvas(&pCanvas);

    Gem::TGemPtr<Canvas::XScene> pScene;
    result = pCanvas->CreateScene(&pScene);

    Gem::TGemPtr<Canvas::XSceneGraphNode> pRootSceneGraphNode;
    result = pScene->QueryInterface(&pRootSceneGraphNode);

    DumpNode(pRootSceneGraphNode, "");
}

