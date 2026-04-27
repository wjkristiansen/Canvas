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
    {
        m_pRootNode = new Gem::TGenericImpl<CUIGraphNodeImpl>();
        m_pRootNode->SetName("UIRoot");
        if (m_pCanvas) m_pRootNode->Register(m_pCanvas);
    }
    return m_pRootNode.Get();
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::CreateNode(XUIGraphNode* pParent, XUIGraphNode** ppNode)
{
    if (!ppNode)
        return Gem::Result::BadPointer;

    XUIGraphNode* pParentNode = pParent ? pParent : GetRootNode();

    Gem::TGemPtr<CUIGraphNodeImpl> pNode = new Gem::TGenericImpl<CUIGraphNodeImpl>();
    pNode->SetName("UINode");
    if (m_pCanvas) pNode->Register(m_pCanvas);
    pParentNode->AddChild(pNode);

    *ppNode = pNode.Detach();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::RemoveElement(XUIElement* pElement)
{
    if (!pElement)
        return Gem::Result::BadPointer;

    // Unbind from node — vertex allocation stays with the element
    XUIGraphNode* pNode = pElement->GetAttachedNode();
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
        return UpdateNode(m_pRootNode.Get());
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::UpdateNode(CUIGraphNodeImpl* pNode)
{
    for (UINT i = 0; i < pNode->GetBoundElementCount(); ++i)
    {
        XUIElement* pElem = pNode->GetBoundElement(i);
        if (!pElem->IsVisible())
            continue;

        Gem::Result result = pElem->Update();
        if (Gem::Failed(result))
            return result;
    }

    // Recurse to child nodes
    XUIGraphNode* pChild = pNode->GetFirstChild();
    while (pChild)
    {
        Gem::Result result = UpdateNode(static_cast<CUIGraphNodeImpl*>(pChild));
        if (Gem::Failed(result))
            return result;
        pChild = pChild->GetNextSibling();
    }

    return Gem::Result::Success;
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

    // Walk node tree: submit nodes with visible content
    std::vector<XUIGraphNode*> stack;
    stack.push_back(m_pRootNode.Get());
    while (!stack.empty())
    {
        XUIGraphNode* pNode = stack.back();
        stack.pop_back();

        bool hasVisibleElements = false;
        UINT elemCount = pNode->GetBoundElementCount();
        for (UINT i = 0; i < elemCount; ++i)
        {
            XUIElement* pElem = pNode->GetBoundElement(i);
            if (!pElem->IsVisible() || !pElem->HasContent())
                continue;

            hasVisibleElements = true;
        }

        if (hasVisibleElements)
            Gem::ThrowGemError(pRenderQueue->SubmitForUIRender(pNode));

        for (XUIGraphNode* pChild = pNode->GetFirstChild(); pChild; pChild = pChild->GetNextSibling())
            stack.push_back(pChild);
    }

    return Gem::Result::Success;
}

} // namespace Canvas
