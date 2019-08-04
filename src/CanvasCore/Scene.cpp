//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CScene::CScene(CCanvas *pCanvas, _In_z_ PCWSTR szName) :
    CSceneGraphNode(pCanvas)
{
    TGemPtr<XSceneGraphNode> pNode;
    std::wstring RootNodeName = std::wstring(szName) + L"_Root";
}

