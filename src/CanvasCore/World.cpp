//================================================================================================
// World
//================================================================================================

#include "pch.h"

#include "World.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CWorld::CWorld(XCanvas *pCanvas, XGfxDevice *pDevice) :
    TCanvasElement(pCanvas),
    m_pDevice(pDevice)
{
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::Initialize()
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
void CWorld::Uninitialize()
{
    // A world object the app still holds outlives its world.
    for (auto &pObject : m_WorldObjects)
        pObject->ClearWorld();
    m_WorldObjects.clear();
    m_ActiveObjectCount = 0;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XSceneGraphNode *) CWorld::GetRootNode()
{
    return m_pRoot.Get();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::CreateWorldObject(XSceneGraphNode *pAnchorNode, XWorldObject **ppObject, const char *name)
{
    CFunctionSentinel sentinel("XWorld::CreateWorldObject", m_pCanvas->GetLogger());

    if (!ppObject)
        return Gem::Result::BadPointer;
    *ppObject = nullptr;

    CSceneGraphNode *pAnchor = nullptr;
    if (pAnchorNode)
    {
        pAnchor = static_cast<CSceneGraphNode *>(pAnchorNode);
        const char *problem = nullptr;
        if (pAnchor->GetAnchoredObject())
            problem = "is already an anchor node";
        else if (pAnchor == m_pRoot.Get())
            problem = "is this world's root node";

        if (problem)
        {
            Canvas::LogError(m_pCanvas->GetLogger(), "XWorld::CreateWorldObject: the anchor node %s", problem);
            return Gem::Result::InvalidArg;
        }
    }

    try
    {
        Gem::TGemPtr<CWorldObject> pObject;
        Gem::ThrowGemError(TCanvasElement<XWorldObject>::CreateAndRegister(
            &pObject, m_pCanvas, name, this, pAnchor));

        // A new world object starts active
        const size_t index = m_WorldObjects.size();
        m_WorldObjects.push_back(pObject);
        pObject->SetWorldIndex(index);
        SwapObjects(index, m_ActiveObjectCount);
        ++m_ActiveObjectCount;

        *ppObject = pObject.Detach();
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
CWorldObject *CWorld::FindObject(XWorldObject *pObject) const
{
    if (!pObject)
        return nullptr;
    CWorldObject *pImpl = static_cast<CWorldObject *>(pObject);
    const size_t index = pImpl->GetWorldIndex();
    if (index >= m_WorldObjects.size() || m_WorldObjects[index].Get() != pImpl)
        return nullptr;
    return pImpl;
}

//------------------------------------------------------------------------------------------------
void CWorld::SwapObjects(size_t a, size_t b)
{
    if (a == b)
        return;
    std::swap(m_WorldObjects[a], m_WorldObjects[b]);
    m_WorldObjects[a]->SetWorldIndex(a);
    m_WorldObjects[b]->SetWorldIndex(b);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::ActivateObject(XWorldObject *pObject)
{
    CWorldObject *pImpl = FindObject(pObject);
    if (!pImpl)
        return Gem::Result::NotFound;

    // Swap it with the first inactive world object and move the boundary past it
    const size_t index = pImpl->GetWorldIndex();
    if (index >= m_ActiveObjectCount)
    {
        SwapObjects(index, m_ActiveObjectCount);
        ++m_ActiveObjectCount;
    }
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::DeactivateObject(XWorldObject *pObject)
{
    CWorldObject *pImpl = FindObject(pObject);
    if (!pImpl)
        return Gem::Result::NotFound;

    // Swap it with the last active world object and move the boundary before it
    const size_t index = pImpl->GetWorldIndex();
    if (index < m_ActiveObjectCount)
    {
        SwapObjects(index, m_ActiveObjectCount - 1);
        --m_ActiveObjectCount;
    }
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::CreateUIGraph(XUIGraph **ppGraph)
{
    if (!ppGraph)
        return Gem::Result::BadPointer;
    *ppGraph = nullptr;

    try
    {
        Gem::TGemPtr<XUIGraph> pGraph;
        Gem::ThrowGemError(m_pCanvas->CreateUIGraph(m_pDevice, &pGraph));
        m_UIGraphs.push_back(pGraph);
        *ppGraph = pGraph.Detach();
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::RemoveUIGraph(XUIGraph *pGraph)
{
    auto it = std::find_if(m_UIGraphs.begin(), m_UIGraphs.end(),
        [pGraph](const Gem::TGemPtr<XUIGraph> &p) { return p.Get() == pGraph; });
    if (!pGraph || it == m_UIGraphs.end())
        return Gem::Result::NotFound;

    m_UIGraphs.erase(it);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::Update(float dtime)
{
    Gem::Result result = m_pRoot->Update(dtime);
    if (Gem::Failed(result))
        return result;

    // An active detached world object's nodes are outside this world's scene graph, so the walk
    // from the root node misses them.  Each is updated from its anchor node, unless it is
    // anchored below another of this world's active world objects, whose update covers it.
    for (size_t i = 0; i < m_ActiveObjectCount; ++i)
    {
        CSceneGraphNode *pAnchor = m_WorldObjects[i]->GetAnchor();
        if (!pAnchor)
            continue;

        bool covered = false;
        for (CSceneGraphNode *pNode = pAnchor->GetParentNode(); pNode && !covered; pNode = pNode->GetParentNode())
        {
            CWorldObject *pAbove = pNode->GetAnchoredObject();
            covered = pNode == m_pRoot.Get() ||
                      (pAbove && pAbove->GetWorld() == this && pAbove->GetWorldIndex() < m_ActiveObjectCount);
        }
        if (covered)
            continue;

        result = pAnchor->Update(dtime);
        if (Gem::Failed(result))
            return result;
    }

    for (auto &pGraph : m_UIGraphs)
    {
        result = pGraph->Update();
        if (Gem::Failed(result))
            return result;
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::SubmitRenderables(XGfxRenderQueue *pRenderQueue)
{
    try
    {
        if (m_pActiveCamera)
            pRenderQueue->SetActiveCamera(m_pActiveCamera);

        pRenderQueue->SetBackground(&m_Background);

        // Auto-build if the caller didn't invoke BuildBVH() at load
        // time.  Eats the first-frame cost rather than failing.
        if (!m_WorldBVH.IsBuilt())
            m_WorldBVH.Build(m_pRoot.Get());
        if (!m_LightBVH.IsBuilt())
            m_LightBVH.Build(m_pRoot.Get());

        // Visibility filter: renderable nodes are gated by frustum
        // visibility; non-renderable nodes (lights, cameras) always
        // pass through.  No active camera => empty visible set =>
        // nothing renderable is submitted.
        m_VisibleNodeScratch.clear();
        if (m_pActiveCamera)
            m_WorldBVH.QueryFrustum(m_pActiveCamera.Get(), m_VisibleNodeScratch);

        // Light visibility: union of untracked lights (ambient /
        // directional / area -- always relevant) and frustum-culled
        // tracked lights.  Order within the list is unspecified; the
        // renderer's filter is set-membership, not rank-keyed.
        m_VisibleLightsScratch.clear();
        const std::vector<XLight*>& untracked = m_LightBVH.GetUntrackedLights();
        m_VisibleLightsScratch.insert(m_VisibleLightsScratch.end(),
                                      untracked.begin(), untracked.end());

        m_VisibleTrackedLightsScratch.clear();
        if (m_pActiveCamera)
            m_LightBVH.QueryFrustum(m_pActiveCamera.Get(), m_VisibleTrackedLightsScratch);

        m_VisibleLightsScratch.insert(m_VisibleLightsScratch.end(),
                                      m_VisibleTrackedLightsScratch.begin(),
                                      m_VisibleTrackedLightsScratch.end());

        // Always install the filter, even when the visible-lights
        // list is empty -- the renderer's contract treats an empty
        // filter as "drop every light this frame," which is the right
        // behavior when culling finds nothing visible (preventing
        // out-of-frustum lights from leaking through SubmitForRender's
        // natural traversal order).
        pRenderQueue->SetVisibleLights(
            m_VisibleLightsScratch.empty() ? nullptr : m_VisibleLightsScratch.data(),
            m_VisibleLightsScratch.size());

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
                const bool renderable = m_WorldBVH.IsRenderableNode(pNode);
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

        // UI graphs draw over the world's scene content, in creation order
        for (auto &pGraph : m_UIGraphs)
            Gem::ThrowGemError(pGraph->SubmitRenderables(pRenderQueue));
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

}