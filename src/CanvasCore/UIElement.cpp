//================================================================================================
// UIElement - Implementation
//================================================================================================

#include "pch.h"
#include "UIElement.h"
#include "FontImpl.h"

namespace Canvas
{

//================================================================================================
// CUIGraphNodeImpl
//================================================================================================

CUIGraphNodeImpl* CUIGraphNodeImpl::GetImpl(XGfxUIGraphNode* pNode)
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
}

CUIGraphNodeImpl::ChildNode* CUIGraphNodeImpl::FindChildNode(XGfxUIGraphNode* pChild)
{
    for (ChildNode* pNode = m_pFirstChild; pNode; pNode = pNode->pNext)
    {
        if (pNode->pNode.Get() == pChild)
            return pNode;
    }
    return nullptr;
}

GEMMETHODIMP CUIGraphNodeImpl::AddChild(_In_ XGfxUIGraphNode* pChild)
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
    }
    else
    {
        ChildNode* pLast = m_pFirstChild;
        while (pLast->pNext)
            pLast = pLast->pNext;
        pLast->pNext = pEntry;
        pEntry->pPrev = pLast;
    }

    return Gem::Result::Success;
}

GEMMETHODIMP CUIGraphNodeImpl::RemoveChild(_In_ XGfxUIGraphNode* pChild)
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

    delete pEntry;
    return Gem::Result::Success;
}

GEMMETHODIMP_(XGfxUIGraphNode*) CUIGraphNodeImpl::GetFirstChild()
{
    return m_pFirstChild ? m_pFirstChild->pNode.Get() : nullptr;
}

GEMMETHODIMP_(XGfxUIGraphNode*) CUIGraphNodeImpl::GetNextSibling()
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

GEMMETHODIMP CUIGraphNodeImpl::BindElement(_In_ XGfxUIElement* pElement)
{
    if (!pElement)
        return Gem::Result::BadPointer;

    XGfxUIGraphNode* pCurrentNode = pElement->GetAttachedNode();
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

    if (pElement->GetType() == UIElementType::Text)
        AsText(pElement)->SetAttachedNode(this);
    else if (pElement->GetType() == UIElementType::Rect)
        AsRect(pElement)->SetAttachedNode(this);

    m_Elements.emplace_back(pElement);
    return Gem::Result::Success;
}

void CUIGraphNodeImpl::UnbindElement(XGfxUIElement* pElement)
{
    for (auto it = m_Elements.begin(); it != m_Elements.end(); ++it)
    {
        if (it->Get() == pElement)
        {
            if (pElement->GetType() == UIElementType::Text)
                AsText(pElement)->SetAttachedNode(nullptr);
            else if (pElement->GetType() == UIElementType::Rect)
                AsRect(pElement)->SetAttachedNode(nullptr);
            m_Elements.erase(it);
            return;
        }
    }
}

//================================================================================================
// CUITextElement
//================================================================================================

void CUITextElement::SetText(PCSTR utf8Text)
{
    if (!utf8Text)
        utf8Text = "";

    if (m_Text == utf8Text)
        return;

    m_Text = utf8Text;
    m_Dirty = true;
}

//------------------------------------------------------------------------------------------------
void CUITextElement::SetFont(XFont* pFont)
{
    if (m_pFont == pFont)
        return;

    m_pFont = pFont;
    m_Dirty = true;
}

//------------------------------------------------------------------------------------------------
void CUITextElement::SetGlyphAtlasInternal(CGlyphAtlasImpl* pAtlas)
{
    if (m_pAtlas == pAtlas)
        return;

    m_pAtlas = pAtlas;
    m_Dirty = true;
}

//------------------------------------------------------------------------------------------------
void CUITextElement::SetLayoutConfig(const TextLayoutConfig& config)
{
    if (m_Config.FontSize == config.FontSize &&
        m_Config.Color.X == config.Color.X && m_Config.Color.Y == config.Color.Y &&
        m_Config.Color.Z == config.Color.Z && m_Config.Color.W == config.Color.W &&
        m_Config.LineHeight == config.LineHeight &&
        m_Config.DisableKerning == config.DisableKerning)
        return;

    m_Config = config;
    m_Dirty = true;
}

//------------------------------------------------------------------------------------------------
void CUITextElement::RegenerateVertices()
{
    m_CachedVertices.clear();

    if (m_Text.empty() || !m_pFont || !m_pAtlas)
        return;

    CTrueTypeFont* pFontData = static_cast<CFont*>(m_pFont)->GetFontData();
    if (!pFontData)
        return;

    std::vector<uint32_t> codepoints;
    Gem::Result result = CTextLayout::DecodeUtf8(m_Text.c_str(), codepoints);
    if (Gem::Failed(result))
        return;

    // Generate vertices in element-local space (origin at 0,0).
    // The node's screen-space position is applied as a per-draw constant by the shader.
    Math::FloatVector3 screenPos(0.0f, 0.0f, 0.0f);

    const CTrueTypeFont::FontMetrics& metrics = pFontData->GetMetrics();
    float lineHeightUnits = (metrics.Ascender - metrics.Descender + metrics.LineGap);
    float lineHeightPixels = lineHeightUnits * (m_Config.FontSize / pFontData->GetUnitsPerEm());

    std::vector<GlyphAtlasEntry> glyphEntries;
    glyphEntries.reserve(codepoints.size());
    Math::FloatVector3 cursorPos = screenPos;

    for (uint32_t codepoint : codepoints)
    {
        if (codepoint == '\n')
        {
            cursorPos.X = screenPos.X;
            cursorPos.Y += lineHeightPixels * m_Config.LineHeight;
            continue;
        }

        if (codepoint == '\t')
        {
            uint32_t spaceCP = ' ';
            for (int i = 0; i < 4; i++)
            {
                GlyphAtlasEntry entry = {};
                result = m_pAtlas->InternalCacheGlyph(spaceCP, m_pFont, entry);
                if (Gem::Failed(result))
                    return;
                cursorPos.X += entry.AdvanceWidth * m_Config.FontSize;
            }
            continue;
        }

        if (codepoint < 32)
            continue;

        GlyphAtlasEntry entry = {};
        result = m_pAtlas->InternalCacheGlyph(codepoint, m_pFont, entry);
        if (Gem::Failed(result))
            return;

        glyphEntries.push_back(entry);
    }

    m_CachedVertices.reserve(glyphEntries.size() * 6);
    cursorPos = screenPos;

    size_t glyphIdx = 0;
    for (uint32_t codepoint : codepoints)
    {
        if (codepoint == '\n')
        {
            cursorPos.X = screenPos.X;
            cursorPos.Y += lineHeightPixels * m_Config.LineHeight;
            continue;
        }

        if (codepoint < 32)
        {
            if (codepoint == '\t')
                glyphIdx += 4;
            continue;
        }

        if (glyphIdx >= glyphEntries.size())
            break;

        float advance = CTextLayout::LayoutGlyph(
            codepoint,
            *pFontData,
            glyphEntries[glyphIdx],
            cursorPos,
            m_Config.Color,
            m_Config.FontSize,
            m_CachedVertices);

        cursorPos.X += advance * m_Config.FontSize;
        glyphIdx++;
    }
}

//================================================================================================
// CUITextElement - Atlas texture access
//================================================================================================

GEMMETHODIMP_(XGfxSurface*) CUITextElement::GetGlyphAtlasTexture()
{
    return m_pAtlas ? m_pAtlas->GetAtlasTexture() : nullptr;
}

//================================================================================================
// CUIRectElement
//================================================================================================

void CUIRectElement::SetSize(const Math::FloatVector2& size)
{
    if (m_Size.X == size.X && m_Size.Y == size.Y)
        return;

    m_Size = size;
    m_Dirty = true;
}

//------------------------------------------------------------------------------------------------
void CUIRectElement::SetFillColor(const Math::FloatVector4& color)
{
    if (m_FillColor.X == color.X && m_FillColor.Y == color.Y &&
        m_FillColor.Z == color.Z && m_FillColor.W == color.W)
        return;

    m_FillColor = color;
    // TODO: Color is still baked into vertices (TextVertex::Color). Once color moves
    // to a per-draw constant, this no longer needs to mark dirty.
    m_Dirty = true;
}

//------------------------------------------------------------------------------------------------
void CUIRectElement::RegenerateVertices()
{
    m_CachedVertices.clear();

    if (m_Size.X <= 0.0f || m_Size.Y <= 0.0f)
        return;

    // Generate vertices in element-local space (origin at 0,0).
    // The node's screen-space position is applied as a per-draw constant by the shader.
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = m_Size.X;
    float y1 = m_Size.Y;

    m_CachedVertices.resize(6);

    auto makeVertex = [](float x, float y, const Math::FloatVector4& color) -> TextVertex
    {
        TextVertex v;
        v.Position = { x, y, 0.0f };
        v.TexCoord = {};
        v.SetColor(color);
        return v;
    };

    m_CachedVertices[0] = makeVertex(x0, y0, m_FillColor);
    m_CachedVertices[1] = makeVertex(x0, y1, m_FillColor);
    m_CachedVertices[2] = makeVertex(x1, y0, m_FillColor);
    m_CachedVertices[3] = makeVertex(x0, y1, m_FillColor);
    m_CachedVertices[4] = makeVertex(x1, y1, m_FillColor);
    m_CachedVertices[5] = makeVertex(x1, y0, m_FillColor);
}

} // namespace Canvas
