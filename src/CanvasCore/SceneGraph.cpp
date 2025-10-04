//================================================================================================
// SceneGraph
//================================================================================================

#pragma once

#include "Transform.h"
#include "SceneGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CSceneGraphNode::CSceneGraphNode(CCanvas *pCanvas) : TCanvasElement(pCanvas), m_pTransform(nullptr)
{
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::Initialize()
{
    CTransform *pTransform = nullptr;
    auto result = Gem::TAggregateGeneric<CTransform, CSceneGraphNode>::Create(&pTransform, this);
    if (Gem::Succeeded(result))
    {
        try
        {
            m_pTransform = pTransform;
        }
        catch(const std::bad_alloc &)
        {
            result = Gem::Result::OutOfMemory;
        }
    }
    
    return result;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CSceneGraphNode::Uninitialize()
{
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::AddChild(_In_ XSceneGraphNode* pChild)
{
    if (!pChild)
        return Gem::Result::InvalidArg;
    
    // TODO: Implement proper child adding logic
    // This is a basic stub implementation
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