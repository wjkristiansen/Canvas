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

    // Defer vertex slot free to next Submit (we don't have XRenderQueue here)
    auto& slot = pCore->GetBufferSlot();
    if (slot.MaxVertexCount > 0)
    {
        m_PendingVertexSlotFrees.push_back({ slot.StartVertex, slot.MaxVertexCount });
        slot.StartVertex = 0;
        slot.MaxVertexCount = 0;
    }

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
        pElement->GetBufferSlot().GpuDirty = true;
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

    // Free vertex slots from removed elements
    for (auto& free : m_PendingVertexSlotFrees)
        pRenderQueue->FreeUITextVertices(free.StartVertex, free.MaxVertexCount);
    m_PendingVertexSlotFrees.clear();

    // Collect visible text elements
    m_VisibleTextElements.clear();
    CollectVisibleTextElements(&m_Root);

    if (m_VisibleTextElements.empty())
        return Gem::Result::Success;

    // Alloc/realloc vertex slots and upload dirty data
    XGfxSurface* pAtlasTexture = nullptr;

    for (CUITextElement* pText : m_VisibleTextElements)
    {
        auto& slot = pText->GetBufferSlot();
        uint32_t vertexCount = pText->GetCachedVertexCount();

        // Allocate or grow the slot if vertex count exceeds capacity
        if (vertexCount > slot.MaxVertexCount)
        {
            if (slot.MaxVertexCount > 0)
                pRenderQueue->FreeUITextVertices(slot.StartVertex, slot.MaxVertexCount);

            uint32_t startVertex = 0;
            Gem::Result r = pRenderQueue->AllocUITextVertices(vertexCount, &startVertex);
            if (Failed(r))
                return r;

            slot.StartVertex = startVertex;
            slot.MaxVertexCount = vertexCount;
            slot.GpuDirty = true;
        }

        // Upload dirty vertex data
        if (slot.GpuDirty && vertexCount > 0)
        {
            Gem::Result r = pRenderQueue->UploadUITextVertices(
                slot.StartVertex, pText->GetCachedVertexData(), vertexCount);
            if (Failed(r))
                return r;
            slot.GpuDirty = false;
        }

        // Track atlas (single shared atlas assumed)
        if (!pAtlasTexture && pText->GetGlyphAtlas())
            pAtlasTexture = pText->GetGlyphAtlas()->GetAtlasTexture();
    }

    if (!pAtlasTexture)
        return Gem::Result::Success;

    // Build draw command array
    m_DrawCommands.clear();
    for (CUITextElement* pText : m_VisibleTextElements)
    {
        uint32_t vertexCount = pText->GetCachedVertexCount();
        if (vertexCount > 0)
        {
            auto& slot = pText->GetBufferSlot();
            m_DrawCommands.push_back({ slot.StartVertex, vertexCount });
        }
    }

    if (m_DrawCommands.empty())
        return Gem::Result::Success;

    return pRenderQueue->DrawUITextBatch(
        m_DrawCommands.data(),
        static_cast<uint32_t>(m_DrawCommands.size()),
        pAtlasTexture);
}

//------------------------------------------------------------------------------------------------
void CUIGraph::CollectVisibleTextElements(CUIElementCore* pElement)
{
    if (!pElement->IsEffectivelyVisible())
        return;

    if (pElement->GetType() == UIElementType::Text)
    {
        auto pText = static_cast<CUITextElement*>(pElement);
        if (pText->GetCachedVertexCount() > 0 && pText->GetGlyphAtlas())
            m_VisibleTextElements.push_back(pText);
    }

    CUIElementCore* pChild = pElement->GetFirstChildCore();
    while (pChild)
    {
        CollectVisibleTextElements(pChild);
        pChild = pChild->GetNextSiblingCore();
    }
}

} // namespace Canvas
