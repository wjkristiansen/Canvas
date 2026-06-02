//================================================================================================
// Scene
//================================================================================================

#include "pch.h"

#include "Scene.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CScene::CScene(XCanvas *pCanvas, XGfxDevice *pDevice) :
    TCanvasElement(pCanvas),
    m_pDevice(pDevice)
{
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CScene::Initialize()
{
    try
    {
        
        Gem::TGemPtr<XSceneGraphNode> pRoot;
        Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&pRoot, "SceneRoot"));
        m_pRoot = pRoot;
    }
    catch(const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XSceneGraphNode *) CScene::GetRootNode()
{
    return m_pRoot.Get();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CScene::SubmitRenderables(XGfxRenderQueue *pRenderQueue)
{
    try
    {
        if (m_pActiveCamera)
            pRenderQueue->SetActiveCamera(m_pActiveCamera);

        pRenderQueue->SetBackground(&m_Background);

        // Auto-build if the caller didn't invoke BuildBVH() at load
        // time.  Eats the first-frame cost rather than failing.
        if (!m_SceneBVH.IsBuilt())
            m_SceneBVH.Build(m_pRoot.Get());

        // Visibility filter: renderable nodes are gated by frustum
        // visibility; non-renderable nodes (lights, cameras) always
        // pass through.  No active camera => empty visible set =>
        // nothing renderable is submitted.
        m_VisibleNodeScratch.clear();
        if (m_pActiveCamera)
            m_SceneBVH.QueryFrustum(m_pActiveCamera.Get(), m_VisibleNodeScratch);

        // Iterative depth-first traversal using persistent stack (no per-frame allocation)
        m_TraversalStack.clear();
        m_TraversalStack.push_back(m_pRoot.Get());

        while (!m_TraversalStack.empty())
        {
            XSceneGraphNode *pNode = m_TraversalStack.back();
            m_TraversalStack.pop_back();

            // Skip only when the node is renderable AND not in the
            // visible set.  Non-renderable nodes pass through; so do
            // renderable nodes the BVH has never seen (e.g. added
            // after Build), so a stale BVH never hides geometry.
            if (pNode->GetBoundElementCount() > 0)
            {
                const bool renderable = m_SceneBVH.IsRenderableNode(pNode);
                const bool visible    = !renderable || m_VisibleNodeScratch.count(pNode) > 0;
                if (visible)
                    Gem::ThrowGemError(pRenderQueue->SubmitForRender(pNode));
            }

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