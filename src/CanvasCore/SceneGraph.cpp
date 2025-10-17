//================================================================================================
// SceneGraph
//================================================================================================

#pragma once

#include "SceneGraph.h"
#include "Camera.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CSceneGraphNode::CSceneGraphNode(CCanvas *pCanvas) :
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
GEMMETHODIMP CSceneGraphNode::Initialize()
{
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CSceneGraphNode::Uninitialize()
{
    m_pFirstChild = nullptr;
    m_pSibling = nullptr;

    TCanvasElement<XSceneGraphNode>::Uninitialize();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::Update(float dtime)
{
    try
    {
        for( auto element : m_Elements)
        {
            Gem::ThrowGemError(element->Update(dtime));
        }

        if(m_pSibling)
        {
            Gem::ThrowGemError(m_pSibling->Update(dtime));
        }

        if(m_pFirstChild)
        {
            Gem::ThrowGemError(m_pFirstChild->Update(dtime));
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

    CSceneGraphNode* pChildNode = CSceneGraphNode::CastFrom(pChild);
    if (!pChildNode)
        return Gem::Result::InvalidArg;

    // If child already has a parent, remove it from old parent's child list
    if (pChildNode->m_pParent)
    {
        CSceneGraphNode* pOldParent = pChildNode->m_pParent;
        
        // Remove from old parent's child list
        if (pOldParent->m_pFirstChild.Get() == pChildNode)
        {
            // Child is the first child, update parent's first child pointer
            pOldParent->m_pFirstChild = pChildNode->m_pSibling;
        }
        else
        {
            // Search for the child in the sibling list
            CSceneGraphNode* pPrev = pOldParent->m_pFirstChild.Get();
            while (pPrev && pPrev->m_pSibling.Get() != pChildNode)
            {
                pPrev = pPrev->m_pSibling.Get();
            }
            
            if (pPrev)
            {
                // Remove child from sibling list
                pPrev->m_pSibling = pChildNode->m_pSibling;
            }
        }
        
        // Clear child's sibling pointer (will be reassigned below if needed)
        pChildNode->m_pSibling = nullptr;
    }

    // Link new parent
    pChildNode->m_pParent = this;
    
    // Mark dirty global state on child (parent changed - but local matrix unaffected)
    // This will also mark cameras and propagate to descendants
    pChildNode->InvalidateTransforms(CSceneGraphNode::DirtyGlobalMatrix | 
                                     CSceneGraphNode::DirtyGlobalRotation | 
                                     CSceneGraphNode::DirtyGlobalTranslation);

    // Append to child list (singly-linked via m_pFirstChild/m_pSibling)
    if (!m_pFirstChild)
    {
        m_pFirstChild = pChildNode;
    }
    else
    {
        CSceneGraphNode* pLast = m_pFirstChild.Get();
        while (pLast->m_pSibling)
        {
            pLast = pLast->m_pSibling.Get();
        }
        pLast->m_pSibling = pChildNode;
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XSceneGraphNode *) CSceneGraphNode::GetParent()
{
    return this->m_pParent;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XSceneGraphNode *) CSceneGraphNode::GetSibling()
{
    return this->m_pSibling.Get();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XSceneGraphNode *) CSceneGraphNode::GetFirstChild()
{
    return this->m_pFirstChild.Get();
}

//------------------------------------------------------------------------------------------------
void CSceneGraphNode::InvalidateTransforms(uint32_t flags)
{
    // Mark all cameras attached to this node as needing to recalculate
    // their view matrices (since this node's world transform changed)
    // Do this FIRST, even if this node's flags are already dirty
    if (flags & (DirtyGlobalMatrix | DirtyGlobalRotation | DirtyGlobalTranslation))
    {
        for (auto& element : m_Elements)
        {
            // Check if this element is a camera and mark its view dirty
            CCamera* pCamera = dynamic_cast<CCamera*>(element.Get());
            if (pCamera)
            {
                pCamera->MarkViewDirty();
            }
        }
    }

    // Check if this node's transforms need updating
    uint32_t requestedFlags = flags & (DirtyLocalMatrix | DirtyGlobalMatrix | 
                                       DirtyGlobalRotation | DirtyGlobalTranslation);
    uint32_t alreadyDirty = m_DirtyFlags & requestedFlags;
    
    // If all requested flags are already dirty, our subtree is already dirty too
    if (alreadyDirty == requestedFlags)
    {
        return;
    }
    
    // Mark this node's transforms dirty
    m_DirtyFlags |= flags;
    
    // Recursively invalidate children
    // Important: Don't propagate DirtyLocalMatrix to children - their local matrices
    // are independent of parent transforms
    uint32_t childFlags = flags & ~DirtyLocalMatrix;
    
    CSceneGraphNode* pChild = m_pFirstChild.Get();
    while (pChild)
    {
        pChild->InvalidateTransforms(childFlags);
        pChild = pChild->m_pSibling.Get();
    }
}

//------------------------------------------------------------------------------------------------
void CSceneGraphNode::BindElement(XSceneGraphElement *pElement)
{
    if (pElement)
    {
        m_Elements.insert(pElement);
    }
}

}