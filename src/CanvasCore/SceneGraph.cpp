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