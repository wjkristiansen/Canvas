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
    return CUIElementCore::GetCore(pElement);
}

//------------------------------------------------------------------------------------------------
Gem::Result CUIGraph::CreateTextElement(XUIElement* pParent, XUITextElement** ppElement)
{
    if (!ppElement)
        return Gem::Result::BadPointer;

    CUIElementCore* pParentCore = pParent ? GetCore(pParent) : &m_Root;
    if (!pParentCore)
        return Gem::Result::InvalidArg;

    // TGenericImpl provides ref counting; element starts with refcount=1 (caller's ref)
    Gem::TGemPtr<CUITextElement> pElement = new Gem::TGenericImpl<CUITextElement>();
    pElement->SetGlyphAtlasInternal(m_pAtlas.get());

    // AddChild AddRefs the element (parent holds a ref via TGemPtr in ChildNode)
    pParentCore->AddChild(pElement);

    *ppElement = pElement.Detach();  // Transfer caller's ref
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

    Gem::TGemPtr<CUIRectElement> pElement = new Gem::TGenericImpl<CUIRectElement>();
    pParentCore->AddChild(pElement);

    *ppElement = pElement.Detach();
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
        m_PendingVertexSlotFrees.push_back({ slot.StartVertex, slot.MaxVertexCount, pCore->GetType() });
        slot.StartVertex = 0;
        slot.MaxVertexCount = 0;
    }

    // RemoveFromParent drops the parent's ref
    pCore->RemoveFromParent();

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

        // Only clear dirty flags if regeneration succeeded.  If a transient
        // resource shortage caused zero vertices for non-empty content, keep
        // the element dirty so it retries on the next update.
        if (pElement->GetCachedVertexCount() > 0 || !pElement->HasContent())
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
    {
        if (free.Type == UIElementType::Rect)
            pRenderQueue->FreeUIRectVertices(free.StartVertex, free.MaxVertexCount);
        else
            pRenderQueue->FreeUITextVertices(free.StartVertex, free.MaxVertexCount);
    }
    m_PendingVertexSlotFrees.clear();

    // --- Rect batch (drawn first, behind text) ---
    m_VisibleRectElements.clear();
    CollectVisibleRectElements(&m_Root);

    for (CUIRectElement* pRect : m_VisibleRectElements)
    {
        auto& slot = pRect->GetBufferSlot();
        uint32_t vertexCount = pRect->GetCachedVertexCount();

        if (vertexCount > slot.MaxVertexCount)
        {
            if (slot.MaxVertexCount > 0)
                pRenderQueue->FreeUIRectVertices(slot.StartVertex, slot.MaxVertexCount);

            uint32_t startVertex = 0;
            Gem::Result r = pRenderQueue->AllocUIRectVertices(vertexCount, &startVertex);
            if (Failed(r))
                return r;

            slot.StartVertex = startVertex;
            slot.MaxVertexCount = vertexCount;
            slot.GpuDirty = true;
        }

        if (slot.GpuDirty && vertexCount > 0)
        {
            Gem::Result r = pRenderQueue->UploadUIRectVertices(
                slot.StartVertex, pRect->GetCachedVertexData(), vertexCount);
            if (Failed(r))
                return r;
            slot.GpuDirty = false;
        }
    }

    m_RectDrawCommands.clear();
    for (CUIRectElement* pRect : m_VisibleRectElements)
    {
        uint32_t vertexCount = pRect->GetCachedVertexCount();
        if (vertexCount > 0)
        {
            auto& slot = pRect->GetBufferSlot();
            m_RectDrawCommands.push_back({ slot.StartVertex, vertexCount });
        }
    }

    if (!m_RectDrawCommands.empty())
    {
        Gem::Result r = pRenderQueue->DrawUIRectBatch(
            m_RectDrawCommands.data(),
            static_cast<uint32_t>(m_RectDrawCommands.size()));
        if (Failed(r))
            return r;
    }

    // --- Text batch (drawn after rects) ---
    m_VisibleTextElements.clear();
    if (!m_pAtlas)
        return Gem::Result::Success;
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

        // Track atlas texture (graph owns the atlas)
        if (!pAtlasTexture)
            pAtlasTexture = m_pAtlas->GetAtlasTexture();
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
        if (pText->GetCachedVertexCount() > 0)
            m_VisibleTextElements.push_back(pText);
    }

    CUIElementCore* pChild = pElement->GetFirstChildCore();
    while (pChild)
    {
        CollectVisibleTextElements(pChild);
        pChild = pChild->GetNextSiblingCore();
    }
}

//------------------------------------------------------------------------------------------------
void CUIGraph::CollectVisibleRectElements(CUIElementCore* pElement)
{
    if (!pElement->IsEffectivelyVisible())
        return;

    if (pElement->GetType() == UIElementType::Rect)
    {
        auto pRect = static_cast<CUIRectElement*>(pElement);
        if (pRect->GetCachedVertexCount() > 0)
            m_VisibleRectElements.push_back(pRect);
    }

    CUIElementCore* pChild = pElement->GetFirstChildCore();
    while (pChild)
    {
        CollectVisibleRectElements(pChild);
        pChild = pChild->GetNextSiblingCore();
    }
}

} // namespace Canvas
