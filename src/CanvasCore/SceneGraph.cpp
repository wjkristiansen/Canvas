//================================================================================================
// SceneGraph
//================================================================================================

#pragma once

#include "SceneGraph.h"

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

    m_LocalMatrixDirty = true;
    m_GlobalRotationDirty = true;
    m_GlobalTranslationDirty = true;
    m_GlobalMatrixDirty = true;
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
    
    // Mark dirty global state on child (parent changed)
    pChildNode->m_GlobalMatrixDirty = true;
    pChildNode->m_GlobalRotationDirty = true;
    pChildNode->m_GlobalTranslationDirty = true;

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

}