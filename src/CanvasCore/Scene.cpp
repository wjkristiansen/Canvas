//================================================================================================
// Scene
//================================================================================================

#include "pch.h"

#include "Scene.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CSceneGraph::CSceneGraph(XCanvas *pCanvas) :
    TCanvasElement(pCanvas)
{
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraph::Initialize()
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
GEMMETHODIMP_(XSceneGraphNode *) CSceneGraph::GetRootSceneGraphNode()
{
    return m_pRoot.Get();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraph::SubmitRenderables(XRenderQueue *pRenderQueue)
{
    try
    {
        if (m_pActiveCamera)
            pRenderQueue->SetActiveCamera(m_pActiveCamera);

        // Iterative depth-first traversal using persistent stack (no per-frame allocation)
        m_TraversalStack.clear();
        m_TraversalStack.push_back(m_pRoot.Get());

        while (!m_TraversalStack.empty())
        {
            XSceneGraphNode *pNode = m_TraversalStack.back();
            m_TraversalStack.pop_back();

            // Submit this node for rendering (render queue handles its elements)
            if (pNode->GetBoundElementCount() > 0)
                Gem::ThrowGemError(pRenderQueue->SubmitForRender(pNode));

            // Push children onto stack for traversal
            for (XSceneGraphNode *pChild = pNode->GetFirstChild(); pChild; pChild = pNode->GetNextChild(pChild))
            {
                m_TraversalStack.push_back(pChild);
            }
        }
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

}