//================================================================================================
// CUIGraph - Implementation
//================================================================================================

#include "pch.h"
#include "UIGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(XGfxUIGraphNode*) CUIGraph::GetRootNode()
{
    if (!m_pRootNode)
    {
        m_pRootNode = new Gem::TGenericImpl<CUIGraphNodeImpl>(m_pCanvas);
        if (m_pCanvas) m_pRootNode->Register(m_pCanvas);
        m_pRootNode->SetName("UIRoot");
    }
    return m_pRootNode.Get();
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::CreateNode(XGfxUIGraphNode* pParent, XGfxUIGraphNode** ppNode)
{
    if (!ppNode)
        return Gem::Result::BadPointer;

    XGfxUIGraphNode* pParentNode = pParent ? pParent : GetRootNode();

    Gem::TGemPtr<CUIGraphNodeImpl> pNode = new Gem::TGenericImpl<CUIGraphNodeImpl>(m_pCanvas);
    if (m_pCanvas) pNode->Register(m_pCanvas);
    pParentNode->AddChild(pNode);

    *ppNode = pNode.Detach();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::CreateTextElement(XGfxUIGraphNode* pNode, XGfxUITextElement** ppElement)
{
    if (!ppElement)
        return Gem::Result::BadPointer;
    if (!pNode)
        pNode = GetRootNode();

    Gem::TGemPtr<CUITextElement> pElement = new Gem::TGenericImpl<CUITextElement>(m_pCanvas);
    if (m_pCanvas) pElement->Register(m_pCanvas);
    pElement->SetGlyphAtlasInternal(m_pAtlas.get());
    pNode->BindElement(pElement);

    *ppElement = pElement.Detach();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::CreateRectElement(XGfxUIGraphNode* pNode, XGfxUIRectElement** ppElement)
{
    if (!ppElement)
        return Gem::Result::BadPointer;
    if (!pNode)
        pNode = GetRootNode();

    Gem::TGemPtr<CUIRectElement> pElement = new Gem::TGenericImpl<CUIRectElement>(m_pCanvas);
    if (m_pCanvas) pElement->Register(m_pCanvas);
    pNode->BindElement(pElement);

    *ppElement = pElement.Detach();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::RemoveElement(XGfxUIElement* pElement)
{
    if (!pElement)
        return Gem::Result::BadPointer;

    // Unbind from node — vertex allocation stays with the element
    XGfxUIGraphNode* pNode = pElement->GetAttachedNode();
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
    for (UINT i = 0; i < pNode->GetBoundElementCount(); ++i)
    {
        XGfxUIElement* pElem = pNode->GetBoundElement(i);
        if (!pElem->IsVisible())
            continue;

        if (pElem->GetType() == UIElementType::Text)
        {
            auto* pText = AsText(pElem);
            if (pText->IsDirty())
                pText->RegenerateVertices();
        }
        else if (pElem->GetType() == UIElementType::Rect)
        {
            auto* pRect = AsRect(pElem);
            if (pRect->IsDirty())
                pRect->RegenerateVertices();
        }
    }

    // Recurse to child nodes
    XGfxUIGraphNode* pChild = pNode->GetFirstChild();
    while (pChild)
    {
        UpdateNode(static_cast<CUIGraphNodeImpl*>(pChild));
        pChild = pChild->GetNextSibling();
    }
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::SubmitRenderables(XGfxRenderQueue* pRenderQueue)
{
    if (!m_pDevice || !m_pRootNode)
        return Gem::Result::Success;
    if (!pRenderQueue)
        return Gem::Result::BadPointer;

    Gem::TGemPtr<XGfxRenderQueue> pGfxRQ;
    Gem::ThrowGemError(pRenderQueue->QueryInterface(&pGfxRQ));

    // Walk node tree: alloc+upload for dirty elements, submit nodes with visible content
    std::vector<XGfxUIGraphNode*> stack;
    stack.push_back(m_pRootNode.Get());
    while (!stack.empty())
    {
        XGfxUIGraphNode* pNode = stack.back();
        stack.pop_back();

        bool hasVisibleElements = false;
        UINT elemCount = pNode->GetBoundElementCount();
        for (UINT i = 0; i < elemCount; ++i)
        {
            XGfxUIElement* pElem = pNode->GetBoundElement(i);
            if (!pElem->IsVisible())
                continue;

            if (pElem->GetType() == UIElementType::Text)
            {
                auto* pText = AsText(pElem);
                if (!pText->HasContent() || pText->GetVertexCount() == 0)
                    continue;
                hasVisibleElements = true;

                if (pText->IsDirty())
                {
                    GfxResourceAllocation newVb{};
                    Gem::ThrowGemError(m_pDevice->AllocVertexBuffer(
                        pText->GetVertexCount(), sizeof(TextVertex), pText->GetVertexData(), pGfxRQ, newVb));
                    pText->SetVertexBuffer(newVb);
                    pText->ClearDirty();
                }
            }
            else if (pElem->GetType() == UIElementType::Rect)
            {
                auto* pRect = AsRect(pElem);
                if (!pRect->HasContent() || pRect->GetVertexCount() == 0)
                    continue;
                hasVisibleElements = true;

                if (pRect->IsDirty())
                {
                    GfxResourceAllocation newVb{};
                    Gem::ThrowGemError(m_pDevice->AllocVertexBuffer(
                        pRect->GetVertexCount(), sizeof(TextVertex), pRect->GetVertexData(), pGfxRQ, newVb));
                    pRect->SetVertexBuffer(newVb);
                    pRect->ClearDirty();
                }
            }
        }

        if (hasVisibleElements)
            Gem::ThrowGemError(pRenderQueue->SubmitForUIRender(pNode));

        for (XGfxUIGraphNode* pChild = pNode->GetFirstChild(); pChild; pChild = pChild->GetNextSibling())
            stack.push_back(pChild);
    }

    return Gem::Result::Success;
}

} // namespace Canvas
