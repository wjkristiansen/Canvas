//================================================================================================
// CUIGraph - Implementation
//================================================================================================

#include "pch.h"
#include "UIGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XUIGraphNode*) CUIGraph::GetRootNode()
{
    if (!m_pRootNode)
        m_pRootNode = new Gem::TGenericImpl<CUIGraphNodeImpl>();
    return m_pRootNode.Get();
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::CreateNode(XUIGraphNode* pParent, XUIGraphNode** ppNode)
{
    if (!ppNode)
        return Gem::Result::BadPointer;

    XUIGraphNode* pParentNode = pParent ? pParent : GetRootNode();

    Gem::TGemPtr<CUIGraphNodeImpl> pNode = new Gem::TGenericImpl<CUIGraphNodeImpl>();
    pParentNode->AddChild(pNode);

    *ppNode = pNode.Detach();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::CreateTextElement(XUIGraphNode* pNode, XUITextElement** ppElement)
{
    if (!ppElement)
        return Gem::Result::BadPointer;
    if (!pNode)
        pNode = GetRootNode();

    Gem::TGemPtr<CUITextElement> pElement = new Gem::TGenericImpl<CUITextElement>();
    pElement->SetGlyphAtlasInternal(m_pAtlas.get());
    pNode->BindElement(pElement);

    *ppElement = pElement.Detach();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::CreateRectElement(XUIGraphNode* pNode, XUIRectElement** ppElement)
{
    if (!ppElement)
        return Gem::Result::BadPointer;
    if (!pNode)
        pNode = GetRootNode();

    Gem::TGemPtr<CUIRectElement> pElement = new Gem::TGenericImpl<CUIRectElement>();
    pNode->BindElement(pElement);

    *ppElement = pElement.Detach();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::RemoveElement(XUIElement* pElement)
{
    if (!pElement)
        return Gem::Result::BadPointer;

    CUIElementState* pState = CUIElementState::GetState(pElement);
    if (!pState)
        return Gem::Result::InvalidArg;

    // Unbind from node
    XUIGraphNode* pNode = pState->GetAttachedNode();
    if (pNode)
    {
        CUIGraphNodeImpl* pImpl = static_cast<CUIGraphNodeImpl*>(pNode);
        pImpl->UnbindElement(pElement);
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::Update()
{
    if (m_pRootNode)
        UpdateNode(m_pRootNode.Get());
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CUIGraph::UpdateNode(CUIGraphNodeImpl* pNode)
{
    // Update bound elements on this node
    for (UINT i = 0; i < pNode->GetBoundElementCount(); ++i)
    {
        XUIElement* pElem = pNode->GetBoundElement(i);
        CUIElementState* pState = CUIElementState::GetState(pElem);
        if (!pState || !pState->IsVisible())
            continue;

        uint32_t dirty = pState->GetDirtyFlags();
        if (dirty & (CUIElementState::DirtyContent | CUIElementState::DirtyPosition))
        {
            pState->RegenerateVertices();

            if (pElem->GetVertexCount() > 0 || !pElem->HasContent())
                pState->ClearDirtyFlags(CUIElementState::DirtyContent | CUIElementState::DirtyPosition);
        }
    }

    // Recurse to child nodes
    XUIGraphNode* pChild = pNode->GetFirstChild();
    while (pChild)
    {
        UpdateNode(static_cast<CUIGraphNodeImpl*>(pChild));
        pChild = pChild->GetNextSibling();
    }
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::SubmitRenderables(XRenderQueue* pRenderQueue)
{
    if (!pRenderQueue || !m_pRootNode)
        return !pRenderQueue ? Gem::Result::BadPointer : Gem::Result::Success;

    std::vector<XUIGraphNode*> stack;
    stack.push_back(m_pRootNode.Get());
    while (!stack.empty())
    {
        XUIGraphNode* pNode = stack.back();
        stack.pop_back();

        if (pNode->GetBoundElementCount() > 0)
            Gem::ThrowGemError(pRenderQueue->SubmitForUIRender(pNode));

        for (XUIGraphNode* pChild = pNode->GetFirstChild(); pChild; pChild = pChild->GetNextSibling())
            stack.push_back(pChild);
    }

    return Gem::Result::Success;
}

} // namespace Canvas
