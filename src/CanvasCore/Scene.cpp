//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CScene::CScene(CCanvas *pCanvas, _In_z_ PCSTR szName) :
    TSceneGraphNode(pCanvas, szName)
{
    TGemPtr<XSceneGraphNode> pNode;
    std::string RootNodeName = std::string(szName) + "_Root";
}

