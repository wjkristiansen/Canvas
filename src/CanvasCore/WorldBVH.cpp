//================================================================================================
// WorldBVH implementation.
//================================================================================================

#include "pch.h"
#include "WorldBVH.h"
#include "SceneGraph.h"

namespace Canvas
{

void WorldBVH::Clear()
{
    for (const RenderableNode& renderable : m_RenderableNodeList)
        m_Tracker.Untrack(renderable.Node);

    m_Primitives.clear();
    m_RenderableNodeList.clear();
    m_RenderableNodes.clear();
    m_BVH = BVH{};
    m_VisiblePrimitiveScratch.clear();
    m_Built = false;
    m_AllBoundsStale = false;
}

void WorldBVH::Build(XSceneNode* root)
{
    Clear();

    if (!root)
        return;

    // Iterative DFS rather than recursion: scene graphs can be very deep
    // (instanced model subtrees, hierarchical rigs) and Canvas already
    // uses this pattern in CWorld::SubmitRenderables for the same reason.
    std::vector<XSceneNode*> stack;
    stack.reserve(64);
    stack.push_back(root);

    BVHPrimitiveId nextId = 0;
    while (!stack.empty())
    {
        XSceneNode* node = stack.back();
        stack.pop_back();
        if (!node)
            continue;

        // A node is renderable when any of its elements has non-empty
        // local bounds (lights, cameras and other non-renderable
        // elements report empty).  Every element of a renderable node
        // gets a primitive, empty ones included, so an element whose
        // bounds become non-empty later (e.g. a mesh given its data)
        // is culled by its own bounds rather than hidden behind its
        // neighbors'.  BVH::Build leaves the empty ones out of the tree.
        const uint32_t elementCount = node->GetBoundElementCount();
        bool nodeRenderable = false;
        for (uint32_t i = 0; i < elementCount && !nodeRenderable; ++i)
            nodeRenderable = !node->GetBoundElement(i)->GetLocalBounds().IsEmpty();

        if (nodeRenderable)
        {
            const Math::FloatMatrix4x4 worldMtx = node->GetGlobalMatrix();
            const uint32_t firstPrimitive = static_cast<uint32_t>(m_Primitives.size());
            for (uint32_t i = 0; i < elementCount; ++i)
            {
                BVHPrimitive prim;
                prim.WorldBounds = node->GetBoundElement(i)->GetLocalBounds().Transform(worldMtx);
                prim.Node        = node;
                prim.Id          = nextId++;
                prim.Flags       = BVHPrimitiveFlags::Static;
                m_Primitives.push_back(prim);
            }

            m_RenderableNodes.emplace(node, static_cast<uint32_t>(m_RenderableNodeList.size()));
            m_RenderableNodeList.push_back({ node, firstPrimitive, elementCount });
            m_Tracker.Track(node);
        }

        for (XSceneNode* child = node->GetFirstChild();
             child;
             child = node->GetNextChild(child))
        {
            stack.push_back(child);
        }
    }

    if (!m_Primitives.empty())
        m_BVH.Build(m_Primitives.data(), m_Primitives.size());

    // Mark built even when the world has no renderable elements;
    // otherwise we'd re-walk the graph every frame.
    m_Built = true;
}

void WorldBVH::Refit()
{
    if (!m_Built)
        return;

    bool changed = false;
    bool emptinessChanged = false;

    // Recomputes the node's primitives' world bounds.
    auto refitNode = [&](const RenderableNode& renderable)
    {
        XSceneNode* node = renderable.Node;
        const Math::FloatMatrix4x4 worldMtx = node->GetGlobalMatrix();
        for (uint32_t i = 0; i < renderable.PrimitiveCount; ++i)
        {
            BVHPrimitive& prim = m_Primitives[renderable.FirstPrimitive + i];
            const Math::AABB worldBounds = node->GetBoundElement(i)->GetLocalBounds().Transform(worldMtx);
            if (worldBounds.IsEmpty() != prim.WorldBounds.IsEmpty())
                emptinessChanged = true;
            prim.WorldBounds = worldBounds;
        }
        changed = true;
    };

    if (m_AllBoundsStale)
    {
        m_AllBoundsStale = false;
        for (const RenderableNode& renderable : m_RenderableNodeList)
            refitNode(renderable);
    }
    else
    {
        for (XSceneNode* node : m_Tracker.GetChangedNodes())
        {
            auto it = m_RenderableNodes.find(node);
            if (it == m_RenderableNodes.end())
                continue; // not renderable at Build; reported for the LightBVH
            refitNode(m_RenderableNodeList[it->second]);
        }
    }

    // BVH::Build leaves empty primitives out of the tree, so a
    // primitive that became non-empty is only reachable after a Build.
    if (emptinessChanged)
        m_BVH.Build(m_Primitives.data(), m_Primitives.size());
    else if (changed)
        m_BVH.Refit(m_Primitives.data(), m_Primitives.size());
}

bool WorldBVH::IsRenderableNode(XSceneNode* node) const
{
    if (!node)
        return false;
    return m_RenderableNodes.find(node) != m_RenderableNodes.end();
}

void WorldBVH::QueryFrustum(XCamera* camera,
                            std::unordered_set<XSceneNode*>& outVisibleNodes) const
{
    outVisibleNodes.clear();
    if (!camera || !m_Built || m_Primitives.empty())
        return;

    const Math::FloatMatrix4x4 vp = camera->GetViewProjectionMatrix();
    const Math::Frustum f = Math::Frustum::FromViewProjection(vp, /*reverseZ*/ true);
    QueryFrustum(f, outVisibleNodes);
}

void WorldBVH::QueryFrustum(const Math::Frustum& frustum,
                            std::unordered_set<XSceneNode*>& outVisibleNodes) const
{
    outVisibleNodes.clear();
    if (!m_Built || m_Primitives.empty())
        return;

    m_VisiblePrimitiveScratch.clear();
    m_BVH.QueryFrustum(m_Primitives.data(), frustum, m_VisiblePrimitiveScratch);

    // Dedup primitives back to nodes.  Reserving against the primitive
    // count is a safe over-estimate that avoids rehashing for typical
    // scenes where most nodes carry a single renderable element.
    outVisibleNodes.reserve(m_VisiblePrimitiveScratch.size());
    for (uint32_t primIdx : m_VisiblePrimitiveScratch)
        outVisibleNodes.insert(m_Primitives[primIdx].Node);
}

} // namespace Canvas
