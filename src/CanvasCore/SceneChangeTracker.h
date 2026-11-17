//================================================================================================
// SceneChangeTracker
//
// Collects the scene nodes whose world-space extent may have changed: a node's global matrix
// changed, an element was bound to it, or one of its bound elements changed its bounds (e.g. a
// mesh instance given new mesh data, a light given a new range).  Only tracked nodes are
// collected, so consumers that cache per-node world-space data (WorldBVH, LightBVH) track the
// nodes they cache and update just the ones reported, rather than rereading every node every
// frame.
//
// It also reports structure changes under a watched root: a node added or removed anywhere
// under it, an element bound to one or unbound, or an element's bounds changing between empty
// and non-empty.  Consumers rebuild their per-node data then, because the tracked set itself is
// out of date.
//
// One tracker serves every consumer in a world.  A node is tracked while any consumer tracks
// it; Track and Untrack nest.  Every consumer reads GetChangedNodes and HasStructureChanged,
// then the owner calls ClearChanges once they all have.
//================================================================================================

#pragma once

#include "pch.h"

namespace Canvas
{

class CSceneNode;
struct XSceneNode;

class SceneChangeTracker
{
public:
    SceneChangeTracker() = default;
    SceneChangeTracker(const SceneChangeTracker&) = delete;
    SceneChangeTracker& operator=(const SceneChangeTracker&) = delete;
    CANVAS_API ~SceneChangeTracker();

    // Start or stop collecting `node`'s changes, nesting: the node is collected until every
    // Track has been matched by an Untrack.  A node is tracked by at most one tracker.
    CANVAS_API void Track(XSceneNode* node);
    CANVAS_API void Untrack(XSceneNode* node);

    // The tracked nodes that changed since the last ClearChanges, each once, in no particular
    // order.
    const std::vector<XSceneNode*>& GetChangedNodes() const { return m_ChangedNodes; }

    // Report structure changes anywhere under `root` (see HasStructureChanged) until this
    // tracker is destroyed.  Holds a reference on `root`.  A root is watched by at most one
    // tracker, and a tracker watches at most one root.
    CANVAS_API void WatchStructure(XSceneNode* root);

    // Whether, since the last ClearChanges, a node was added or removed anywhere under the
    // watched root, or an element was bound to one.
    bool HasStructureChanged() const { return m_StructureChanged; }

    // Empties the changed list and clears HasStructureChanged.  Brings each listed node's global matrix up to date first, so
    // its next transform change reaches it again: a node whose global matrix is already stale
    // does not pass a change on to its subtree, whose matrices are stale too and which are
    // still listed.
    CANVAS_API void ClearChanges();

    // Called by a tracked node when it changes.
    void NotifyChanged(CSceneNode* node);

    // Called by a node under the watched root when its structure changes.
    void NotifyStructureChanged() { m_StructureChanged = true; }

private:
    void RemoveChanged(CSceneNode* node);

    std::vector<XSceneNode*> m_ChangedNodes;
    size_t m_TrackedNodeCount = 0;
    Gem::TGemPtr<XSceneNode> m_pWatchedRoot;
    bool m_StructureChanged = false;
};

} // namespace Canvas
