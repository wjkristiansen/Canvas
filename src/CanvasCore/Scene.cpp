//================================================================================================
// Scene
//================================================================================================

#include "pch.h"

#include "Scene.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CScene::CScene(CCanvas *pCanvas) :
    TCanvasElement(pCanvas),
    m_pCanvas(pCanvas)
{
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CScene::Initialize()
{
    try
    {
        
        Gem::TGemPtr<XSceneGraphNode> pRoot;
        Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&pRoot));
        m_pRoot = CSceneGraphNode::CastFrom(pRoot.Get());
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