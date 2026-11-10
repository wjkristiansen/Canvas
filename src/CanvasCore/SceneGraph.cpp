//================================================================================================
// SceneGraph
//================================================================================================

#pragma once

#include "SceneGraph.h"
#include "Camera.h"
#include "WorldObject.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CSceneGraphNode::CSceneGraphNode(XCanvas *pCanvas) :
    TCanvasElement(pCanvas)
{
    // Initialize cached matrices/quaternions
    m_LocalMatrix = Canvas::Math::IdentityMatrix<float, 4, 4>();
    m_GlobalRotation = Canvas::Math::IdentityQuaternion<float>();
    m_GlobalMatrix = Canvas::Math::IdentityMatrix<float, 4, 4>();

    // All dirty flags are already set via member initialization (m_DirtyFlags = TransformDirtyFlags::All)
}

//------------------------------------------------------------------------------------------------
Gem::Result CSceneGraphNode::Initialize()
{
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CSceneGraphNode::Uninitialize()
{
    // Clean up all child wrapper nodes.  A child held elsewhere, such as the anchor node of a
    // world object, outlives this node, so its parent pointer is cleared first.
    ChildNode* pChild = m_pFirstChild;
    while (pChild)
    {
        ChildNode* pNext = pChild->m_pNext;
        static_cast<CSceneGraphNode*>(pChild->m_pNode.Get())->m_pParent = nullptr;
        delete pChild;
        pChild = pNext;
    }
    m_pFirstChild = nullptr;
    m_pLastChild = nullptr;
    m_ChildMap.clear();
    m_pAnimController = nullptr;

    // An element held elsewhere, such as the active camera of a world, outlives this node, so
    // its pointer to this node is cleared
    for (auto &pElement : m_Elements)
    {
        if (pElement->GetAttachedNode() == this)
            pElement->NotifyNodeContextChanged(nullptr);
    }
    m_Elements.clear();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XWorldObject *) CSceneGraphNode::GetWorldObject()
{
    for (CSceneGraphNode *pNode = this; pNode; pNode = pNode->m_pParent)
    {
        if (pNode->m_pAnchoredObject)
            return pNode->m_pAnchoredObject;
    }
    return nullptr;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::Update(float dtime)
{
    try
    {
        // Tick the animation controller first so the transforms it writes onto this
        // subtree are in place before spatial elements and children observe them.
        if (m_pAnimController)
        {
            Gem::ThrowGemError(m_pAnimController->Update(dtime));
        }

        for (auto& element : m_Elements)
        {
            Gem::ThrowGemError(element->Update(dtime));
        }

        for (ChildNode* pChild = m_pFirstChild; pChild; pChild = pChild->m_pNext)
        {
            Gem::ThrowGemError(pChild->m_pNode->Update(dtime));
        }
    }
    catch(const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
// Unlinks pWrapper from this node's child list, leaving its map entry and the wrapper itself.
void CSceneGraphNode::UnlinkWrapper(ChildNode *pWrapper)
{
    if (pWrapper->m_pPrev)
        pWrapper->m_pPrev->m_pNext = pWrapper->m_pNext;
    else
        m_pFirstChild = pWrapper->m_pNext;

    if (pWrapper->m_pNext)
        pWrapper->m_pNext->m_pPrev = pWrapper->m_pPrev;
    else
        m_pLastChild = pWrapper->m_pPrev;

    pWrapper->m_pPrev = nullptr;
    pWrapper->m_pNext = nullptr;
}

//------------------------------------------------------------------------------------------------
// Makes pChild a child of this node, placed before pBefore, which is one of this node's child
// wrappers, or last when pBefore is nullptr.  pChild moves from its parent in one step: the new
// wrapper holds it before the old parent releases it, and nothing can fail after it leaves its
// old parent.
Gem::Result CSceneGraphNode::MoveChild(XSceneGraphNode *pChild, ChildNode *pBefore)
{
    CSceneGraphNode *pChildNode = static_cast<CSceneGraphNode *>(pChild);

    // A node is never its own ancestor
    for (CSceneGraphNode *pNode = this; pNode; pNode = pNode->m_pParent)
    {
        if (pNode == pChildNode)
        {
            Canvas::LogError(m_pCanvas->GetLogger(),
                "XSceneGraphNode: a node cannot be added to its own subtree");
            return Gem::Result::InvalidArg;
        }
    }

    ChildNode *pWrapper = nullptr;
    if (pChildNode->m_pParent == this)
    {
        // A move within this node's children reuses its wrapper
        pWrapper = m_ChildMap.at(pChild);
        if (pWrapper == pBefore)
            return Gem::Result::Success;
        UnlinkWrapper(pWrapper);
    }
    else
    {
        try
        {
            auto pNew = std::make_unique<ChildNode>();
            pNew->m_pNode = pChild;
            m_ChildMap.emplace(pChild, pNew.get());
            pWrapper = pNew.release();
        }
        catch (const std::bad_alloc &)
        {
            return Gem::Result::OutOfMemory;
        }

        if (pChildNode->m_pParent)
            pChildNode->m_pParent->ReleaseChild(pChildNode);
    }

    // Link before pBefore, or last
    pWrapper->m_pNext = pBefore;
    pWrapper->m_pPrev = pBefore ? pBefore->m_pPrev : m_pLastChild;
    if (pWrapper->m_pPrev)
        pWrapper->m_pPrev->m_pNext = pWrapper;
    else
        m_pFirstChild = pWrapper;
    if (pBefore)
        pBefore->m_pPrev = pWrapper;
    else
        m_pLastChild = pWrapper;

    pChildNode->m_pParent = this;
    pChildNode->InvalidateTransforms(TransformDirtyFlags::GlobalMatrix | TransformDirtyFlags::GlobalRotation);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
// Takes pChild, one of this node's children, out of its child list.  Its world transform is
// then its local transform.  Releasing the wrapper may destroy pChild.
void CSceneGraphNode::ReleaseChild(CSceneGraphNode *pChild)
{
    auto it = m_ChildMap.find(pChild);
    ChildNode *pWrapper = it->second;
    m_ChildMap.erase(it);
    UnlinkWrapper(pWrapper);

    pChild->m_pParent = nullptr;
    pChild->InvalidateTransforms(TransformDirtyFlags::GlobalMatrix | TransformDirtyFlags::GlobalRotation);
    delete pWrapper;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::AddChild(_In_ XSceneGraphNode* pChild)
{
    if (!pChild)
        return Gem::Result::InvalidArg;
    return MoveChild(pChild, nullptr);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::RemoveChild(_In_ XSceneGraphNode* pChild)
{
    if (!pChild || m_ChildMap.find(pChild) == m_ChildMap.end())
        return Gem::Result::InvalidArg;
    ReleaseChild(static_cast<CSceneGraphNode *>(pChild));
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::InsertChildBefore(_In_ XSceneGraphNode* pChild, _In_ XSceneGraphNode* pSibling)
{
    if (!pChild || !pSibling || pChild == pSibling)
        return Gem::Result::InvalidArg;
    auto it = m_ChildMap.find(pSibling);
    if (it == m_ChildMap.end())
        return Gem::Result::InvalidArg;
    return MoveChild(pChild, it->second);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::InsertChildAfter(_In_ XSceneGraphNode* pChild, _In_ XSceneGraphNode* pSibling)
{
    if (!pChild || !pSibling || pChild == pSibling)
        return Gem::Result::InvalidArg;
    auto it = m_ChildMap.find(pSibling);
    if (it == m_ChildMap.end())
        return Gem::Result::InvalidArg;

    // Placed before the node after pSibling.  When that node is pChild itself, pChild is
    // already in place.
    ChildNode *pBefore = it->second->m_pNext;
    if (pBefore && pBefore->m_pNode.Get() == pChild)
        return Gem::Result::Success;
    return MoveChild(pChild, pBefore);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XSceneGraphNode *) CSceneGraphNode::GetParent()
{
    return this->m_pParent;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XSceneGraphNode *) CSceneGraphNode::GetNextChild(_In_ XSceneGraphNode* pCurrent)
{
    if (!pCurrent)
        return nullptr;

    // Find wrapper for current child
    auto it = m_ChildMap.find(pCurrent);
    if (it == m_ChildMap.end())
        return nullptr;

    ChildNode* pWrapper = it->second;
    if (!pWrapper->m_pNext)
        return nullptr;

    return pWrapper->m_pNext->m_pNode.Get();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XSceneGraphNode *) CSceneGraphNode::GetPrevChild(_In_ XSceneGraphNode* pCurrent)
{
    if (!pCurrent)
        return nullptr;

    // Find wrapper for current child
    auto it = m_ChildMap.find(pCurrent);
    if (it == m_ChildMap.end())
        return nullptr;

    ChildNode* pWrapper = it->second;
    if (!pWrapper->m_pPrev)
        return nullptr;

    return pWrapper->m_pPrev->m_pNode.Get();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XSceneGraphNode *) CSceneGraphNode::GetFirstChild()
{
    if (!m_pFirstChild)
        return nullptr;
    return m_pFirstChild->m_pNode.Get();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::BindElement(_In_ XSceneGraphElement *pElement)
{
    if (pElement)
    {
        // Avoid duplicate bindings
        for (auto& e : m_Elements)
        {
            if (e.Get() == pElement)
                return Gem::Result::Success;
        }
        m_Elements.push_back(pElement);
        
        // Notify element of attachment via the public interface
        Gem::ThrowGemError(pElement->NotifyNodeContextChanged(this));
    }
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CSceneGraphNode::InvalidateTransforms(TransformDirtyFlags flags)
{
    // Check if this node's transforms need updating
    TransformDirtyFlags requestedFlags = flags & TransformDirtyFlags::All;
    TransformDirtyFlags alreadyDirty = m_DirtyFlags & requestedFlags;
    
    // If all requested flags are already dirty, our subtree is already dirty too
    // BUT we still need to notify attached elements that global transform is dirty
    if (alreadyDirty == requestedFlags)
    {
        // Notify attached elements that the global transform is dirty
        if (HasAnyFlag(flags, TransformDirtyFlags::GlobalMatrix | TransformDirtyFlags::GlobalRotation))
        {
            for (auto& element : m_Elements)
            {
                element->NotifyNodeContextChanged(this);
            }
        }
        return;
    }
    
    // Notify attached elements that the global transform is dirty
    if (HasAnyFlag(flags, TransformDirtyFlags::GlobalMatrix | TransformDirtyFlags::GlobalRotation))
    {
        for (auto& element : m_Elements)
        {
            element->NotifyNodeContextChanged(this);
        }
    }

    // Mark this node's transforms dirty
    m_DirtyFlags |= flags;
    
    // Recursively invalidate children
    // Important: Don't propagate LocalMatrix to children - their local matrices
    // are independent of parent transforms
    TransformDirtyFlags childFlags = flags & ~TransformDirtyFlags::LocalMatrix;
    
    for (ChildNode* pWrapper = m_pFirstChild; pWrapper; pWrapper = pWrapper->m_pNext)
    {
        static_cast<CSceneGraphNode *>(pWrapper->m_pNode.Get())->InvalidateTransforms(childFlags);
    }
}

}
