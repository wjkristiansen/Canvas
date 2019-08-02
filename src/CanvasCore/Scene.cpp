//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CScene::CScene(CCanvas *pCanvas, _In_z_ PCWSTR szName) :
    m_ObjectName(this, szName, pCanvas), // throw(Gem::GemError)
    CSceneGraphNode(pCanvas)
{
    TGemPtr<XSceneGraphNode> pNode;
    std::wstring RootNodeName = std::wstring(szName) + L"_Root";
}

