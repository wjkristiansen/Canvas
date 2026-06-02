//================================================================================================
// SceneBVH implementation.
//================================================================================================

#include "pch.h"
#include "SceneBVH.h"

namespace Canvas
{

void SceneBVH::Clear()
{
    m_Primitives.clear();
    m_RenderableNodes.clear();
    m_BVH = BVH{};
    m_VisiblePrimitiveScratch.clear();
    m_Built = false;
}

void SceneBVH::Build(XSceneGraphNode* root)
{
    Clear();

    if (!root)
        return;

    // Iterative DFS rather than recursion: scene graphs can be very deep
    // (instanced model subtrees, hierarchical rigs) and Canvas already
    // uses this pattern in CScene::SubmitRenderables for the same reason.
    std::vector<XSceneGraphNode*> stack;
    stack.reserve(64);
    stack.push_back(root);

    BVHPrimitiveId nextId = 0;
    while (!stack.empty())
    {
        XSceneGraphNode* node = stack.back();
        stack.pop_back();
        if (!node)
            continue;

        // Compute the global matrix ONCE per node and reuse across all of
        // its elements.  GetGlobalMatrix may cascade through dirty-flag
        // propagation; not paying that cost per element matters for nodes
        // carrying multiple bound elements.
        const Math::FloatMatrix4x4 worldMtx = node->GetGlobalMatrix();

        const UINT elementCount = node->GetBoundElementCount();
        bool nodeContributed = false;
        for (UINT i = 0; i < elementCount; ++i)
        {
            XSceneGraphElement* element = node->GetBoundElement(i);
            if (!element)
                continue;

            const Math::AABB localBounds = element->GetLocalBounds();
            if (localBounds.IsEmpty())
                continue; // non-renderable (lights, cameras) or genuinely empty

            const Math::AABB worldBounds = localBounds.Transform(worldMtx);
            if (worldBounds.IsEmpty())
                continue; // transform of empty stays empty; defensive double-check

            BVHPrimitive prim;
            prim.WorldBounds = worldBounds;
            prim.Node        = node;
            prim.Id          = nextId++;
            prim.Flags       = BVHPrimitiveFlag_Static;
            m_Primitives.push_back(prim);
            nodeContributed = true;
        }

        if (nodeContributed)
            m_RenderableNodes.insert(node);

        for (XSceneGraphNode* child = node->GetFirstChild();
             child;
             child = node->GetNextChild(child))
        {
            stack.push_back(child);
        }
    }

    if (!m_Primitives.empty())
        m_BVH.Build(m_Primitives.data(), m_Primitives.size());

    // Mark built even when the scene has no renderable elements;
    // otherwise we'd re-walk the graph every frame.
    m_Built = true;
}

bool SceneBVH::IsRenderableNode(XSceneGraphNode* node) const
{
    if (!node)
        return false;
    return m_RenderableNodes.find(node) != m_RenderableNodes.end();
}

void SceneBVH::QueryFrustum(XCamera* camera,
                            std::unordered_set<XSceneGraphNode*>& outVisibleNodes) const
{
    outVisibleNodes.clear();
    if (!camera || !m_Built || m_Primitives.empty())
        return;

    const Math::FloatMatrix4x4 vp = camera->GetViewProjectionMatrix();
    const Math::Frustum f = Math::Frustum::FromViewProjection(vp, /*reverseZ*/ true);
    QueryFrustum(f, outVisibleNodes);
}

void SceneBVH::QueryFrustum(const Math::Frustum& frustum,
                            std::unordered_set<XSceneGraphNode*>& outVisibleNodes) const
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
