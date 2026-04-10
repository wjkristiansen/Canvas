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

    // Defer vertex slot free to next Submit
    auto& slot = pState->GetBufferSlot();
    if (slot.MaxVertexCount > 0)
    {
        m_PendingVertexSlotFrees.push_back({ slot.StartVertex, slot.MaxVertexCount, pState->GetType() });
        slot.StartVertex = 0;
        slot.MaxVertexCount = 0;
    }

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
        CUIElementState* pState = CUIElementState::GetState(pNode->GetBoundElement(i));
        if (!pState || !pState->IsVisible())
            continue;

        uint32_t dirty = pState->GetDirtyFlags();
        if (dirty & (CUIElementState::DirtyContent | CUIElementState::DirtyPosition))
        {
            pState->RegenerateVertices();
            pState->GetBufferSlot().GpuDirty = true;

            if (pState->GetCachedVertexCount() > 0 || !pState->HasContent())
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
    if (m_pRootNode)
        CollectVisibleRectElements(m_pRootNode.Get());

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
    if (m_pRootNode)
        CollectVisibleTextElements(m_pRootNode.Get());

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
void CUIGraph::CollectVisibleTextElements(CUIGraphNodeImpl* pNode)
{
    for (UINT i = 0; i < pNode->GetBoundElementCount(); ++i)
    {
        XUIElement* pElem = pNode->GetBoundElement(i);
        CUIElementState* pState = CUIElementState::GetState(pElem);
        if (pState && pState->IsVisible() && pState->GetType() == UIElementType::Text)
        {
            m_VisibleTextElements.push_back(static_cast<CUITextElement*>(static_cast<XUITextElement*>(pElem)));
        }
    }

    XUIGraphNode* pChild = pNode->GetFirstChild();
    while (pChild)
    {
        CollectVisibleTextElements(static_cast<CUIGraphNodeImpl*>(pChild));
        pChild = pChild->GetNextSibling();
    }
}

//------------------------------------------------------------------------------------------------
void CUIGraph::CollectVisibleRectElements(CUIGraphNodeImpl* pNode)
{
    for (UINT i = 0; i < pNode->GetBoundElementCount(); ++i)
    {
        XUIElement* pElem = pNode->GetBoundElement(i);
        CUIElementState* pState = CUIElementState::GetState(pElem);
        if (pState && pState->IsVisible() && pState->GetType() == UIElementType::Rect)
        {
            m_VisibleRectElements.push_back(static_cast<CUIRectElement*>(static_cast<XUIRectElement*>(pElem)));
        }
    }

    XUIGraphNode* pChild = pNode->GetFirstChild();
    while (pChild)
    {
        CollectVisibleRectElements(static_cast<CUIGraphNodeImpl*>(pChild));
        pChild = pChild->GetNextSibling();
    }
}

} // namespace Canvas
