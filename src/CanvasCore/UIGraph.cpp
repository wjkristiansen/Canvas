//================================================================================================
// CUIGraph - Implementation
//================================================================================================

#include "pch.h"
#include "UIGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CUIElementCore* CUIGraph::GetCore(XUIElement* pElement)
{
    if (!pElement)
        return nullptr;

    switch (pElement->GetType())
    {
    case UIElementType::Text:
        return static_cast<CUITextElement*>(static_cast<XUITextElement*>(pElement));
    case UIElementType::Rect:
        return static_cast<CUIRectElement*>(static_cast<XUIRectElement*>(pElement));
    default:
        return nullptr;
    }
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::CreateTextElement(XUIElement* pParent, XUITextElement** ppElement)
{
    if (!ppElement)
        return Gem::Result::BadPointer;

    CUIElementCore* pParentCore = pParent ? GetCore(pParent) : &m_Root;
    if (!pParentCore)
        return Gem::Result::InvalidArg;

    auto pElement = std::make_unique<CUITextElement>();
    CUITextElement* pRaw = pElement.get();
    pParentCore->AddChild(pRaw);
    m_OwnedElements.push_back(std::move(pElement));

    *ppElement = pRaw;
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::CreateRectElement(XUIElement* pParent, XUIRectElement** ppElement)
{
    if (!ppElement)
        return Gem::Result::BadPointer;

    CUIElementCore* pParentCore = pParent ? GetCore(pParent) : &m_Root;
    if (!pParentCore)
        return Gem::Result::InvalidArg;

    auto pElement = std::make_unique<CUIRectElement>();
    CUIRectElement* pRaw = pElement.get();
    pParentCore->AddChild(pRaw);
    m_OwnedElements.push_back(std::move(pElement));

    *ppElement = pRaw;
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::RemoveElement(XUIElement* pElement)
{
    if (!pElement)
        return Gem::Result::BadPointer;

    CUIElementCore* pCore = GetCore(pElement);
    if (!pCore)
        return Gem::Result::InvalidArg;

    pCore->RemoveFromParent();

    for (auto it = m_OwnedElements.begin(); it != m_OwnedElements.end(); ++it)
    {
        if (it->get() == pCore)
        {
            m_OwnedElements.erase(it);
            break;
        }
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::Update()
{
    UpdateElement(&m_Root);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CUIGraph::UpdateElement(CUIElementCore* pElement)
{
    if (!pElement->IsEffectivelyVisible())
        return;

    uint32_t dirty = pElement->GetDirtyFlags();
    if (dirty & (CUIElementCore::DirtyContent | CUIElementCore::DirtyPosition))
    {
        pElement->RegenerateVertices();
        pElement->ClearDirtyFlags(CUIElementCore::DirtyContent | CUIElementCore::DirtyPosition);
    }

    if (dirty & CUIElementCore::DirtyVisibility)
        pElement->ClearDirtyFlags(CUIElementCore::DirtyVisibility);

    CUIElementCore* pChild = pElement->GetFirstChildCore();
    while (pChild)
    {
        UpdateElement(pChild);
        pChild = pChild->GetNextSiblingCore();
    }
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::Submit(XRenderQueue* pRenderQueue)
{
    if (!pRenderQueue)
        return Gem::Result::BadPointer;

    SubmitElement(&m_Root, pRenderQueue);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CUIGraph::SubmitElement(CUIElementCore* pElement, XRenderQueue* pRenderQueue)
{
    if (!pElement->IsEffectivelyVisible())
        return;

    if (pElement->GetType() == UIElementType::Text)
    {
        CUITextElement* pText = static_cast<CUITextElement*>(pElement);
        uint32_t vertexCount = pText->GetCachedVertexCount();
        if (vertexCount > 0 && pText->GetGlyphAtlas())
        {
            XGfxSurface* pAtlasTexture = pText->GetGlyphAtlas()->GetAtlasTexture();
            if (pAtlasTexture)
            {
                const Math::FloatVector2& pos = pText->GetAbsolutePosition();
                pRenderQueue->DrawText(
                    pText->GetCachedVertexData(),
                    vertexCount,
                    pAtlasTexture,
                    Math::FloatVector4(pos.X, pos.Y, 0.0f, 0.0f));
            }
        }
    }

    CUIElementCore* pChild = pElement->GetFirstChildCore();
    while (pChild)
    {
        SubmitElement(pChild, pRenderQueue);
        pChild = pChild->GetNextSiblingCore();
    }
}

} // namespace Canvas
