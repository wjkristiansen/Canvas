//================================================================================================
// SceneChangeTracker implementation.
//================================================================================================

#include "pch.h"
#include "SceneChangeTracker.h"
#include "SceneGraph.h"

#include <cassert>

namespace Canvas
{

SceneChangeTracker::~SceneChangeTracker()
{
    // Consumers untrack their nodes before the tracker goes away; a node still pointing here
    // would notify a destroyed tracker.
    assert(m_TrackedNodeCount == 0);

    if (m_pWatchedRoot)
        static_cast<CSceneNode*>(m_pWatchedRoot.Get())->m_pStructureTracker = nullptr;
}

void SceneChangeTracker::WatchStructure(XSceneNode* root)
{
    CSceneNode* pRoot = static_cast<CSceneNode*>(root);
    assert(!m_pWatchedRoot && !pRoot->m_pStructureTracker);
    pRoot->m_pStructureTracker = this;
    m_pWatchedRoot = root;
}

void SceneChangeTracker::Track(XSceneNode* node)
{
    CSceneNode* pNode = static_cast<CSceneNode*>(node);
    assert(!pNode->m_pChangeTracker || pNode->m_pChangeTracker == this);
    pNode->m_pChangeTracker = this;
    if (pNode->m_ChangeTrackCount++ == 0)
        ++m_TrackedNodeCount;
}

void SceneChangeTracker::Untrack(XSceneNode* node)
{
    CSceneNode* pNode = static_cast<CSceneNode*>(node);
    assert(pNode->m_pChangeTracker == this && pNode->m_ChangeTrackCount > 0);
    if (--pNode->m_ChangeTrackCount > 0)
        return;

    RemoveChanged(pNode);
    pNode->m_pChangeTracker = nullptr;
    --m_TrackedNodeCount;
}

void SceneChangeTracker::NotifyChanged(CSceneNode* node)
{
    if (node->m_ChangedIndex != CSceneNode::kNotChanged)
        return;
    node->m_ChangedIndex = static_cast<uint32_t>(m_ChangedNodes.size());
    m_ChangedNodes.push_back(node);
}

void SceneChangeTracker::ClearChanges()
{
    for (XSceneNode* node : m_ChangedNodes)
    {
        node->GetGlobalMatrix();
        static_cast<CSceneNode*>(node)->m_ChangedIndex = CSceneNode::kNotChanged;
    }
    m_ChangedNodes.clear();
    m_StructureChanged = false;
}

void SceneChangeTracker::RemoveChanged(CSceneNode* node)
{
    const uint32_t index = node->m_ChangedIndex;
    if (index == CSceneNode::kNotChanged)
        return;

    XSceneNode* last = m_ChangedNodes.back();
    m_ChangedNodes[index] = last;
    static_cast<CSceneNode*>(last)->m_ChangedIndex = index;
    m_ChangedNodes.pop_back();
    node->m_ChangedIndex = CSceneNode::kNotChanged;
}

} // namespace Canvas
