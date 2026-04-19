//================================================================================================
// SceneGraph
//================================================================================================

#pragma once

#include "SceneGraph.h"
#include "Camera.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CSceneGraphNode::CSceneGraphNode(XCanvas *pCanvas) :
    TCanvasElement(pCanvas)
{
    // Initialize local transform to identity
    m_LocalRotation = Canvas::Math::IdentityQuaternion<float>();
    m_LocalTranslation = Canvas::Math::FloatVector4(0.0f, 0.0f, 0.0f, 1.0f);
    m_LocalScale = Canvas::Math::FloatVector4(1.0f, 1.0f, 1.0f, 0.0f);

    // Initialize cached matrices/quaternions
    m_LocalMatrix = Canvas::Math::IdentityMatrix<float, 4, 4>();
    m_GlobalRotation = Canvas::Math::IdentityQuaternion<float>();
    m_GlobalTranslation = Canvas::Math::FloatVector4(0.0f, 0.0f, 0.0f, 1.0f);
    m_GlobalMatrix = Canvas::Math::IdentityMatrix<float, 4, 4>();

    // All dirty flags are already set via member initialization (m_DirtyFlags = DirtyAll)
}

//------------------------------------------------------------------------------------------------
Gem::Result CSceneGraphNode::Initialize()
{
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CSceneGraphNode::Uninitialize()
{
    // Clean up all child wrapper nodes
    ChildNode* pChild = m_pFirstChild;
    while (pChild)
    {
        ChildNode* pNext = pChild->m_pNext;
        delete pChild;
        pChild = pNext;
    }
    m_pFirstChild = nullptr;
    m_pLastChild = nullptr;
    m_ChildMap.clear();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::Update(float dtime)
{
    try
    {
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
GEMMETHODIMP CSceneGraphNode::AddChild(_In_ XSceneGraphNode* pChild)
{
    if (!pChild)
        return Gem::Result::InvalidArg;

    // If child already has a parent, remove it from old parent's child list
    XSceneGraphNode* pCurrentParent = pChild->GetParent();
    if (pCurrentParent)
    {
        Gem::ThrowGemError(pCurrentParent->RemoveChild(pChild));
    }

    // Create wrapper node for intrusive linking
    ChildNode* pWrapper = new ChildNode();
    pWrapper->m_pNode = pChild;

    // For CSceneGraphNode children, update internal parent pointer
    CSceneGraphNode* pChildNode = dynamic_cast<CSceneGraphNode*>(pChild);
    if (pChildNode)
    {
        pChildNode->m_pParent = this;
        
        // Mark dirty global state on child (parent changed - but local matrix unaffected)
        pChildNode->InvalidateTransforms(CSceneGraphNode::DirtyGlobalMatrix | 
                                         CSceneGraphNode::DirtyGlobalRotation | 
                                         CSceneGraphNode::DirtyGlobalTranslation);
    }

    // Append to child list
    if (!m_pFirstChild)
    {
        m_pFirstChild = pWrapper;
        m_pLastChild = pWrapper;
    }
    else
    {
        pWrapper->m_pPrev = m_pLastChild;
        m_pLastChild->m_pNext = pWrapper;
        m_pLastChild = pWrapper;
    }

    m_ChildMap[pChild] = pWrapper;

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::RemoveChild(_In_ XSceneGraphNode* pChild)
{
    if (!pChild)
        return Gem::Result::InvalidArg;

    // Find wrapper in map
    auto it = m_ChildMap.find(pChild);
    if (it == m_ChildMap.end())
    {
        return Gem::Result::InvalidArg;  // Child not found
    }

    ChildNode* pWrapper = it->second;

    // Unlink from doubly-linked list
    if (pWrapper->m_pPrev)
    {
        pWrapper->m_pPrev->m_pNext = pWrapper->m_pNext;
    }
    else
    {
        m_pFirstChild = pWrapper->m_pNext;
    }

    if (pWrapper->m_pNext)
    {
        pWrapper->m_pNext->m_pPrev = pWrapper->m_pPrev;
    }
    else
    {
        m_pLastChild = pWrapper->m_pPrev;
    }

    // For CSceneGraphNode children, clear internal parent pointer
    CSceneGraphNode* pChildNode = dynamic_cast<CSceneGraphNode*>(pChild);
    if (pChildNode)
    {
        pChildNode->m_pParent = nullptr;
    }

    m_ChildMap.erase(it);
    delete pWrapper;

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::InsertChildBefore(_In_ XSceneGraphNode* pChild, _In_ XSceneGraphNode* pSibling)
{
    if (!pChild || !pSibling)
        return Gem::Result::InvalidArg;

    // If child already has a parent, remove it from old parent's child list
    XSceneGraphNode* pCurrentParent = pChild->GetParent();
    if (pCurrentParent)
    {
        Gem::ThrowGemError(pCurrentParent->RemoveChild(pChild));
    }

    // Find sibling wrapper
    auto it = m_ChildMap.find(pSibling);
    if (it == m_ChildMap.end())
    {
        return Gem::Result::InvalidArg;  // Sibling not found
    }

    ChildNode* pSiblingWrapper = it->second;

    // Create wrapper node for the child
    ChildNode* pWrapper = new ChildNode();
    pWrapper->m_pNode = pChild;

    // For CSceneGraphNode children, update internal parent pointer
    CSceneGraphNode* pChildNode = dynamic_cast<CSceneGraphNode*>(pChild);
    if (pChildNode)
    {
        pChildNode->m_pParent = this;
        pChildNode->InvalidateTransforms(CSceneGraphNode::DirtyGlobalMatrix | 
                                         CSceneGraphNode::DirtyGlobalRotation | 
                                         CSceneGraphNode::DirtyGlobalTranslation);
    }

    // Insert before sibling in the doubly-linked list
    pWrapper->m_pNext = pSiblingWrapper;
    pWrapper->m_pPrev = pSiblingWrapper->m_pPrev;

    if (pSiblingWrapper->m_pPrev)
    {
        pSiblingWrapper->m_pPrev->m_pNext = pWrapper;
    }
    else
    {
        m_pFirstChild = pWrapper;
    }

    pSiblingWrapper->m_pPrev = pWrapper;

    m_ChildMap[pChild] = pWrapper;

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::InsertChildAfter(_In_ XSceneGraphNode* pChild, _In_ XSceneGraphNode* pSibling)
{
    if (!pChild || !pSibling)
        return Gem::Result::InvalidArg;

    // If child already has a parent, remove it from old parent's child list
    XSceneGraphNode* pCurrentParent = pChild->GetParent();
    if (pCurrentParent)
    {
        Gem::ThrowGemError(pCurrentParent->RemoveChild(pChild));
    }

    // Find sibling wrapper
    auto it = m_ChildMap.find(pSibling);
    if (it == m_ChildMap.end())
    {
        return Gem::Result::InvalidArg;  // Sibling not found
    }

    ChildNode* pSiblingWrapper = it->second;

    // Create wrapper node for the child
    ChildNode* pWrapper = new ChildNode();
    pWrapper->m_pNode = pChild;

    // For CSceneGraphNode children, update internal parent pointer
    CSceneGraphNode* pChildNode = dynamic_cast<CSceneGraphNode*>(pChild);
    if (pChildNode)
    {
        pChildNode->m_pParent = this;
        pChildNode->InvalidateTransforms(CSceneGraphNode::DirtyGlobalMatrix | 
                                         CSceneGraphNode::DirtyGlobalRotation | 
                                         CSceneGraphNode::DirtyGlobalTranslation);
    }

    // Insert after sibling in the doubly-linked list
    pWrapper->m_pPrev = pSiblingWrapper;
    pWrapper->m_pNext = pSiblingWrapper->m_pNext;

    if (pSiblingWrapper->m_pNext)
    {
        pSiblingWrapper->m_pNext->m_pPrev = pWrapper;
    }
    else
    {
        m_pLastChild = pWrapper;
    }

    pSiblingWrapper->m_pNext = pWrapper;

    m_ChildMap[pChild] = pWrapper;

    return Gem::Result::Success;
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
GEMMETHODIMP CSceneGraphNode::BindElement(XSceneGraphElement *pElement)
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
void CSceneGraphNode::InvalidateTransforms(uint32_t flags)
{
    // Check if this node's transforms need updating
    uint32_t requestedFlags = flags & (DirtyLocalMatrix | DirtyGlobalMatrix | 
                                       DirtyGlobalRotation | DirtyGlobalTranslation);
    uint32_t alreadyDirty = m_DirtyFlags & requestedFlags;
    
    // If all requested flags are already dirty, our subtree is already dirty too
    // BUT we still need to notify attached elements that global transform is dirty
    if (alreadyDirty == requestedFlags)
    {
        // Notify attached elements that the global transform is dirty
        if (flags & (DirtyGlobalMatrix | DirtyGlobalRotation | DirtyGlobalTranslation))
        {
            for (auto& element : m_Elements)
            {
                element->NotifyNodeContextChanged(this);
            }
        }
        return;
    }
    
    // Notify attached elements that the global transform is dirty
    if (flags & (DirtyGlobalMatrix | DirtyGlobalRotation | DirtyGlobalTranslation))
    {
        for (auto& element : m_Elements)
        {
            element->NotifyNodeContextChanged(this);
        }
    }

    // Mark this node's transforms dirty
    m_DirtyFlags |= flags;
    
    // Recursively invalidate children
    // Important: Don't propagate DirtyLocalMatrix to children - their local matrices
    // are independent of parent transforms
    uint32_t childFlags = flags & ~DirtyLocalMatrix;
    
    for (ChildNode* pWrapper = m_pFirstChild; pWrapper; pWrapper = pWrapper->m_pNext)
    {
        // Only invalidate our own implementation children
        // External implementations manage their own transform invalidation
        CSceneGraphNode* pChildImpl = dynamic_cast<CSceneGraphNode*>(pWrapper->m_pNode.Get());
        if (pChildImpl)
        {
            pChildImpl->InvalidateTransforms(childFlags);
        }
    }
}

}
