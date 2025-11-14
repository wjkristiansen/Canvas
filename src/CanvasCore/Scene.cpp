//================================================================================================
// Scene
//================================================================================================

#include "pch.h"

#include "Scene.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CScene::CScene(XCanvas *pCanvas) :
    TCanvasElement(pCanvas)
{
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CScene::Initialize()
{
    try
    {
        
        Gem::TGemPtr<XSceneGraphNode> pRoot;
        Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&pRoot));
        m_pRoot = pRoot;
    }
    catch(const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XSceneGraphNode *) CScene::GetRootSceneGraphNode()
{
    return m_pRoot.Get();
}

}