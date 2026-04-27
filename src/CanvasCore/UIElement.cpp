//================================================================================================
// UIElement - Implementation
//================================================================================================

#include "pch.h"
#include "UIElement.h"

namespace Canvas
{

//================================================================================================
// CUIGraphNodeImpl
//================================================================================================

CUIGraphNodeImpl* CUIGraphNodeImpl::GetImpl(XUIGraphNode* pNode)
{
    return static_cast<CUIGraphNodeImpl*>(pNode);
}

CUIGraphNodeImpl::~CUIGraphNodeImpl()
{
    ChildNode* pChild = m_pFirstChild;
    while (pChild)
    {
        CUIGraphNodeImpl* pImpl = GetImpl(pChild->pNode);
        if (pImpl)
        {
            pImpl->m_pParent = nullptr;
            pImpl->m_pMyEntry = nullptr;
        }
        ChildNode* pNext = pChild->pNext;
        delete pChild;
        pChild = pNext;
    }
    m_pFirstChild = nullptr;
    m_pLastChild = nullptr;
}

CUIGraphNodeImpl::ChildNode* CUIGraphNodeImpl::FindChildNode(XUIGraphNode* pChild)
{
    for (ChildNode* pNode = m_pFirstChild; pNode; pNode = pNode->pNext)
    {
        if (pNode->pNode.Get() == pChild)
            return pNode;
    }
    return nullptr;
}

GEMMETHODIMP CUIGraphNodeImpl::AddChild(_In_ XUIGraphNode* pChild)
{
    if (!pChild)
        return Gem::Result::BadPointer;

    CUIGraphNodeImpl* pImpl = GetImpl(pChild);
    if (!pImpl || pImpl->m_pParent == this)
        return Gem::Result::Success;

    if (pImpl->m_pParent)
        pImpl->m_pParent->RemoveChild(pChild);

    pImpl->m_pParent = this;

    ChildNode* pEntry = new ChildNode();
    pEntry->pNode = pChild;
    pImpl->m_pMyEntry = pEntry;

    if (!m_pFirstChild)
    {
        m_pFirstChild = pEntry;
        m_pLastChild = pEntry;
    }
    else
    {
        pEntry->pPrev = m_pLastChild;
        m_pLastChild->pNext = pEntry;
        m_pLastChild = pEntry;
    }

    return Gem::Result::Success;
}

GEMMETHODIMP CUIGraphNodeImpl::RemoveChild(_In_ XUIGraphNode* pChild)
{
    if (!pChild)
        return Gem::Result::BadPointer;

    ChildNode* pEntry = FindChildNode(pChild);
    if (!pEntry)
        return Gem::Result::Success;

    CUIGraphNodeImpl* pImpl = GetImpl(pChild);
    if (pImpl)
    {
        pImpl->m_pParent = nullptr;
        pImpl->m_pMyEntry = nullptr;
    }

    if (pEntry->pPrev)
        pEntry->pPrev->pNext = pEntry->pNext;
    else
        m_pFirstChild = pEntry->pNext;

    if (pEntry->pNext)
        pEntry->pNext->pPrev = pEntry->pPrev;
    else
        m_pLastChild = pEntry->pPrev;

    delete pEntry;
    return Gem::Result::Success;
}

GEMMETHODIMP_(XUIGraphNode*) CUIGraphNodeImpl::GetFirstChild()
{
    return m_pFirstChild ? m_pFirstChild->pNode.Get() : nullptr;
}

GEMMETHODIMP_(XUIGraphNode*) CUIGraphNodeImpl::GetNextSibling()
{
    if (!m_pMyEntry || !m_pMyEntry->pNext)
        return nullptr;
    return m_pMyEntry->pNext->pNode.Get();
}

void CUIGraphNodeImpl::SetLocalPosition(const Math::FloatVector2& position)
{
    if (m_LocalPosition.X == position.X && m_LocalPosition.Y == position.Y)
        return;

    m_LocalPosition = position;
}

GEMMETHODIMP_(Math::FloatVector2) CUIGraphNodeImpl::GetGlobalPosition()
{
    if (m_pParent)
    {
        Math::FloatVector2 parentGlobal = m_pParent->GetGlobalPosition();
        return Math::FloatVector2(parentGlobal.X + m_LocalPosition.X,
                                  parentGlobal.Y + m_LocalPosition.Y);
    }
    return m_LocalPosition;
}

GEMMETHODIMP CUIGraphNodeImpl::BindElement(_In_ XUIElement* pElement)
{
    if (!pElement)
        return Gem::Result::BadPointer;

    XUIGraphNode* pCurrentNode = pElement->GetAttachedNode();
    if (pCurrentNode == this)
    {
        // Already bound to this node; nothing to do.
        return Gem::Result::Success;
    }

    if (pCurrentNode != nullptr)
    {
        // Detach from the previous owner so a single element is never present
        // in two nodes' bound-element lists at once.
        static_cast<CUIGraphNodeImpl*>(pCurrentNode)->UnbindElement(pElement);
    }

    pElement->NotifyNodeContextChanged(this);

    m_Elements.emplace_back(pElement);
    return Gem::Result::Success;
}

void CUIGraphNodeImpl::UnbindElement(XUIElement* pElement)
{
    for (auto it = m_Elements.begin(); it != m_Elements.end(); ++it)
    {
        if (it->Get() == pElement)
        {
            pElement->Detach();
            m_Elements.erase(it);
            return;
        }
    }
}

} // namespace Canvas
