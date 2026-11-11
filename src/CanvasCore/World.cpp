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
        
        Gem::TGemPtr<XSceneNode> pRoot;
        Gem::ThrowGemError(m_pCanvas->CreateSceneNode(&pRoot, "SceneRoot"));
        m_pRoot = pRoot;
        static_cast<CSceneNode *>(pRoot.Get())->SetWorldRoot();
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
    // Teardown delivers no callbacks
    m_Events.clear();
    m_Listeners.clear();
    m_UniqueNames.clear();

    // A world object the app still holds outlives its world.
    for (auto &pObject : m_WorldObjects)
        pObject->ClearWorld();
    m_WorldObjects.clear();
    m_ActiveObjectCount = 0;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XSceneNode *) CWorld::GetRootNode()
{
    return m_pRoot.Get();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::AddWorldObject(XWorldObject *pObject)
{
    if (!pObject)
        return Gem::Result::BadPointer;

    CWorldObject *pImpl = static_cast<CWorldObject *>(pObject);
    const char *problem = nullptr;
    Gem::Result failure = Gem::Result::InvalidArg;
    if (pImpl->GetCanvas() != m_pCanvas)
        problem = "was created by another canvas";
    else if (pImpl->GetWorld())
    {
        problem = "is already in a world";
        failure = Gem::Result::AlreadyExists;
    }
    else if (pImpl->GetDevice() && pImpl->GetDevice() != m_pDevice.Get())
        problem = "instantiates a model whose GPU resources are on another device";

    if (problem)
    {
        Canvas::LogError(m_pCanvas->GetLogger(), "XWorld::AddWorldObject: the world object %s", problem);
        return failure;
    }

    try
    {
        // A world object starts active in the world it joins
        const size_t index = m_WorldObjects.size();
        m_WorldObjects.push_back(pImpl);
        pImpl->SetWorldIndex(index);
        pImpl->SetWorld(this);
        SwapObjects(index, m_ActiveObjectCount);
        ++m_ActiveObjectCount;
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
void CWorld::EraseObject(size_t index)
{
    // Keep the active range contiguous, then move the object to the end and drop it
    if (index < m_ActiveObjectCount)
    {
        SwapObjects(index, m_ActiveObjectCount - 1);
        index = --m_ActiveObjectCount;
    }
    SwapObjects(index, m_WorldObjects.size() - 1);
    ReleaseUniqueName(m_WorldObjects.back());
    m_WorldObjects.back()->ClearWorld();
    m_WorldObjects.pop_back();
}

//------------------------------------------------------------------------------------------------
Gem::Result CWorld::CheckActiveCamera()
{
    if (!m_pActiveCamera)
        return Gem::Result::Success;
    // The camera is in this world's scene graph when this world's root node is its node or an
    // ancestor of it, wherever the root node is
    XSceneNode *pNode = m_pActiveCamera->GetAttachedNode();
    while (pNode && pNode != m_pRoot.Get())
        pNode = pNode->GetParent();
    if (!pNode)
    {
        const char *cameraName = m_pActiveCamera->GetName();
        const char *worldName = GetName();
        Canvas::LogWarn(m_pCanvas->GetLogger(),
            "XWorld '%s': active camera '%s' is not in the scene graph; the world has no active camera "
            "and renders only its background until one is set",
            worldName ? worldName : "", cameraName ? cameraName : "");
        m_pActiveCamera = nullptr;
        try
        {
            m_Events.push_back({ EventType::ActiveCameraLost, nullptr });
        }
        catch (const std::bad_alloc &)
        {
            return Gem::Result::OutOfMemory;
        }
    }
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
// The scale, rotation, and translation of an affine matrix (row vectors).  A matrix sheared by
// non-uniform scale under rotation has no exact decomposition; the shear is lost.
static Transform TransformFromMatrix(const Math::FloatMatrix4x4 &m)
{
    Math::FloatVector4 rows[3] = { m[0], m[1], m[2] };
    float scale[3];
    for (int i = 0; i < 3; ++i)
        scale[i] = std::sqrt(Math::DotProduct(rows[i], rows[i]));

    // A mirrored basis gets a negative X scale
    if (Math::DotProduct(Math::CrossProduct(rows[0], rows[1]), rows[2]) < 0.0f)
        scale[0] = -scale[0];

    Math::FloatMatrix4x4 rotation = Math::IdentityMatrix<float, 4, 4>();
    for (int i = 0; i < 3; ++i)
    {
        const float inv = scale[i] != 0.0f ? 1.0f / scale[i] : 0.0f;
        for (int c = 0; c < 3; ++c)
            rotation[i][c] = rows[i][c] * inv;
    }

    Transform t;
    t.Scale       = Math::FloatVector4(scale[0], scale[1], scale[2], 0.0f);
    t.Rotation    = Math::QuaternionFromRotationMatrix(rotation);
    t.Translation = Math::FloatVector4(m[3][0], m[3][1], m[3][2], 1.0f);
    return t;
}

//------------------------------------------------------------------------------------------------
// Adds pNode as the last child of pParent, or leaves it with no parent when pParent is nullptr,
// with the local transform that keeps the given world matrix.
static Gem::Result ReparentKeepingWorld(XSceneNode *pNode, XSceneNode *pParent,
                                        const Math::FloatMatrix4x4 &world)
{
    Math::FloatMatrix4x4 local = world;
    if (pParent)
        local = world * Math::AffineInverse(pParent->GetGlobalMatrix());

    Gem::Result result = Gem::Result::Success;
    if (pParent)
        result = pParent->AddChild(pNode);
    else if (pNode->GetParent())
        result = pNode->GetParent()->RemoveChild(pNode);
    if (Gem::Failed(result))
        return result;

    pNode->SetLocalTransform(TransformFromMatrix(local));
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::PlaceObject(XWorldObject *pObject, XSceneNode *pParent, bool keepWorldTransform)
{
    CWorldObject *pImpl = FindObject(pObject);
    if (!pImpl)
        return Gem::Result::NotFound;

    CSceneNode *pObjectRoot = pImpl->GetRoot();
    if (!pObjectRoot || !pParent)
        return Gem::Result::InvalidArg;

    if (pParent->GetCanvas() != m_pCanvas)
    {
        Canvas::LogError(m_pCanvas->GetLogger(), "XWorld::PlaceObject: the parent node was created by another canvas");
        return Gem::Result::InvalidArg;
    }

    // The new parent must be outside the root node's subtree
    for (XSceneNode *pNode = pParent; pNode; pNode = pNode->GetParent())
    {
        if (pNode == pObjectRoot)
        {
            Canvas::LogError(m_pCanvas->GetLogger(),
                "XWorld::PlaceObject: the parent node is in the world object's root node's subtree");
            return Gem::Result::InvalidArg;
        }
    }

    try
    {
        if (keepWorldTransform)
            Gem::ThrowGemError(ReparentKeepingWorld(pObjectRoot, pParent, pObjectRoot->GetGlobalMatrix()));
        else
            Gem::ThrowGemError(pParent->AddChild(pObjectRoot));
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::DetachObject(XWorldObject *pObject)
{
    CWorldObject *pImpl = FindObject(pObject);
    if (!pImpl)
        return Gem::Result::NotFound;

    CSceneNode *pObjectRoot = pImpl->GetRoot();
    if (pObjectRoot && pObjectRoot->GetParent())
    {
        Gem::Result result = pObjectRoot->GetParent()->RemoveChild(pObjectRoot);
        if (Gem::Failed(result))
            return result;
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::ReleaseWorldObject(XWorldObject *pObject)
{
    CWorldObject *pImpl = FindObject(pObject);
    if (!pImpl)
        return Gem::Result::NotFound;

    try
    {
        // Find the world objects the release orphans: those attached to the released world object's
        // nodes, which are its root node and the nodes below it down to other root nodes.  Their world matrices
        // are taken before any node moves.
        struct Orphan { Gem::TGemPtr<CWorldObject> pObject; Math::FloatMatrix4x4 world; };
        std::vector<Orphan> orphans;
        std::vector<XSceneNode *> stack;

        if (pImpl->GetRoot())
            stack.push_back(pImpl->GetRoot());
        while (!stack.empty())
        {
            XSceneNode *pNode = stack.back();
            stack.pop_back();
            for (XSceneNode *pChild = pNode->GetFirstChild(); pChild; pChild = pNode->GetNextChild(pChild))
            {
                CWorldObject *pOrphan = static_cast<CSceneNode *>(pChild)->GetRootedObject();
                if (pOrphan && pOrphan->GetWorld() == this)
                    orphans.push_back({ pOrphan, pChild->GetGlobalMatrix() });
                else
                    stack.push_back(pChild);
            }
        }

        // Take the released world object's root node out of the graph, then attach each orphan
        // to the node the release detached from
        CSceneNode *pObjectRoot = pImpl->GetRoot();
        XSceneNode *pDetachedFrom = pObjectRoot ? pObjectRoot->GetParent() : nullptr;
        if (pDetachedFrom)
            Gem::ThrowGemError(pDetachedFrom->RemoveChild(pObjectRoot));

        for (Orphan &orphan : orphans)
        {
            Gem::ThrowGemError(ReparentKeepingWorld(orphan.pObject->GetRoot(), pDetachedFrom, orphan.world));
            m_Events.push_back({ EventType::ObjectOrphaned, orphan.pObject });
        }

        EraseObject(pImpl->GetWorldIndex());
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
// Erases pObject's unique-name entry, if it has one, and clears its pointer to the entry's string.
void CWorld::ReleaseUniqueName(CWorldObject *pObject)
{
    if (!FindObject(pObject))
    {
        Canvas::LogError(m_pCanvas->GetLogger(),
            "CWorld::ReleaseUniqueName: the world object is not one of this world's world objects");
        return;
    }

    const std::string *pName = pObject->GetUniqueNamePtr();
    if (!pName)
        return;

    auto it = m_UniqueNames.find(std::string_view(*pName));
    if (it == m_UniqueNames.end() || it->second != pObject)
    {
        Canvas::LogError(m_pCanvas->GetLogger(),
            "CWorld::ReleaseUniqueName: unique name '%s' is not this world object's entry", pName->c_str());
        return;
    }

    m_UniqueNames.erase(it);
    pObject->SetUniqueNamePtr(nullptr);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::SetUniqueName(XWorldObject *pObject, const char *uniqueName)
{
    CWorldObject *pImpl = FindObject(pObject);
    if (!pImpl)
        return Gem::Result::NotFound;
    if (uniqueName && !*uniqueName)
        return Gem::Result::InvalidArg;

    const std::string *pCurrent = pImpl->GetUniqueNamePtr();
    if (uniqueName && pCurrent && *pCurrent == uniqueName)
        return Gem::Result::Success;

    // Claiming the new unique name is the only step that can fail, so it comes before the
    // current one is released
    const std::string *pNew = nullptr;
    if (uniqueName)
    {
        if (m_UniqueNames.find(std::string_view(uniqueName)) != m_UniqueNames.end())
        {
            const char *name = pObject->GetName();
            Canvas::LogError(m_pCanvas->GetLogger(),
                "XWorld::SetUniqueName: '%s' is already the unique name of another world object; "
                "world object '%s' keeps its unique name", uniqueName, name ? name : "");
            return Gem::Result::AlreadyExists;
        }

        try
        {
            pNew = &m_UniqueNames.emplace(uniqueName, pImpl).first->first;
        }
        catch (const std::bad_alloc &)
        {
            return Gem::Result::OutOfMemory;
        }
    }

    ReleaseUniqueName(pImpl);
    pImpl->SetUniqueNamePtr(pNew);

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XWorldObject *) CWorld::FindObject(const char *uniqueName)
{
    if (!uniqueName)
        return nullptr;
    auto it = m_UniqueNames.find(std::string_view(uniqueName));
    return it != m_UniqueNames.end() ? it->second : nullptr;
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
GEMMETHODIMP CWorld::RegisterListener(WorldListener *pListener)
{
    if (!pListener)
        return Gem::Result::InvalidArg;
    if (std::find(m_Listeners.begin(), m_Listeners.end(), pListener) != m_Listeners.end())
        return Gem::Result::AlreadyExists;

    try
    {
        m_Listeners.push_back(pListener);
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::UnregisterListener(WorldListener *pListener)
{
    auto it = pListener ? std::find(m_Listeners.begin(), m_Listeners.end(), pListener) : m_Listeners.end();
    if (it == m_Listeners.end())
        return Gem::Result::NotFound;

    // During delivery the list is being walked, so the slot is cleared and compacted afterward
    if (m_Delivering)
        *it = nullptr;
    else
        m_Listeners.erase(it);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
// Delivers each queued event to each listener, in order.  Events that callbacks cause are
// queued behind the rest and delivered in the same pass.  An orphan no longer in this world
// gets no callback.  The active camera is checked first and after each event's callbacks.
Gem::Result CWorld::DeliverEvents()
{
    m_Delivering = true;
    Gem::Result result = CheckActiveCamera();
    for (size_t e = 0; Gem::Succeeded(result) && e < m_Events.size(); ++e)
    {
        Event event = m_Events[e];  // A copy, since callbacks may queue events
        if (event.type == EventType::ObjectOrphaned && !FindObject(event.pObject.Get()))
            continue;

        for (size_t l = 0; l < m_Listeners.size(); ++l)
        {
            WorldListener *pListener = m_Listeners[l];
            if (!pListener)
                continue;
            switch (event.type)
            {
            case EventType::ObjectOrphaned:
                pListener->OnObjectOrphaned(this, event.pObject.Get());
                break;
            case EventType::ActiveCameraLost:
                pListener->OnActiveCameraLost(this);
                break;
            }
        }

        // A callback may have moved the active camera
        result = CheckActiveCamera();
    }
    m_Events.clear();
    m_Listeners.erase(std::remove(m_Listeners.begin(), m_Listeners.end(), nullptr), m_Listeners.end());
    m_Delivering = false;
    return result;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CWorld::Update(float dtime)
{
    if (m_Delivering)
    {
        Canvas::LogError(m_pCanvas->GetLogger(), "XWorld::Update: called from a WorldListener callback");
        return Gem::Result::Unavailable;
    }

    Gem::Result result = DeliverEvents();
    if (Gem::Failed(result))
        return result;

    for (size_t i = 0; i < m_ActiveObjectCount; ++i)
    {
        result = m_WorldObjects[i]->Update(dtime);
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
            XSceneNode *pNode = m_TraversalStack.back();
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
            for (XSceneNode *pChild = pNode->GetFirstChild(); pChild; pChild = pNode->GetNextChild(pChild))
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