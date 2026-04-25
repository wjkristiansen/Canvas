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

    if (pElement->GetType() == UIElementType::Text)
        AsText(pElement)->SetAttachedNode(this);
    else if (pElement->GetType() == UIElementType::Rect)
        AsRect(pElement)->SetAttachedNode(this);

    m_Elements.emplace_back(pElement);
    return Gem::Result::Success;
}

void CUIGraphNodeImpl::UnbindElement(XUIElement* pElement)
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
void CUITextElement::SetLayoutConfig(const TextLayoutConfig& config)
{
    // Glyph instances depend on font size, line height, and kerning.
    // Color is a per-draw constant, so color-only changes skip regeneration.
    bool geometryChanged = m_Config.FontSize != config.FontSize ||
                           m_Config.LineHeight != config.LineHeight ||
                           m_Config.DisableKerning != config.DisableKerning;

    m_Config = config;

    if (geometryChanged)
        m_Dirty = true;
}

//------------------------------------------------------------------------------------------------
Gem::Result CUITextElement::RegenerateGlyphs()
{
    m_CachedGlyphs.clear();

    if (m_Text.empty())
        return Gem::Result::Success;

    if (!m_pFont)
        return Gem::Result::BadPointer;
    if (!m_pGlyphCache)
        return Gem::Result::BadPointer;

    CTrueTypeFont* pFontData = static_cast<CFont*>(m_pFont)->GetFontData();
    if (!pFontData)
        return Gem::Result::BadPointer;

    std::vector<uint32_t> codepoints;
    Gem::Result result = CTextLayout::DecodeUtf8(m_Text.c_str(), codepoints);
    if (Gem::Failed(result))
        return result;

    // Generate glyph instances in element-local space (origin at 0,0).
    // The node's screen-space position is applied as a per-draw constant by the shader.
    const CTrueTypeFont::FontMetrics& metrics = pFontData->GetMetrics();
    float lineHeightUnits = (metrics.Ascender - metrics.Descender + metrics.LineGap);
    float lineHeightPixels = lineHeightUnits * (m_Config.FontSize / pFontData->GetUnitsPerEm());

    m_CachedGlyphs.reserve(codepoints.size());
    float cursorX = 0.0f;
    float cursorY = 0.0f;

    for (uint32_t codepoint : codepoints)
    {
        if (codepoint == '\n')
        {
            cursorX = 0.0f;
            cursorY += lineHeightPixels * m_Config.LineHeight;
            continue;
        }

        if (codepoint == '\t')
        {
            GlyphAtlasEntry spaceEntry = {};
            result = m_pGlyphCache->CacheGlyphForFont(' ', m_pFont, spaceEntry);
            if (Gem::Failed(result))
                return result;
            cursorX += spaceEntry.AdvanceWidth * m_Config.FontSize * 4.0f;
            continue;
        }

        if (codepoint < 32)
            continue;

        GlyphAtlasEntry entry = {};
        result = m_pGlyphCache->CacheGlyphForFont(codepoint, m_pFont, entry);
        if (Gem::Failed(result))
            return result;

        float glyphWidth  = entry.BitmapWidth  * m_Config.FontSize;
        float glyphHeight = entry.BitmapHeight * m_Config.FontSize;

        // Only emit an instance for glyphs with visible bitmap data.
        // Spaces have AdvanceWidth but no bitmap, so they just advance the cursor.
        if (glyphWidth > 0.0f && glyphHeight > 0.0f)
        {
            GlyphInstance gi;
            gi.Offset.x = cursorX + entry.LeftBearing * m_Config.FontSize;
            gi.Offset.y = cursorY + entry.TopBearing  * m_Config.FontSize;
            gi.Size.x = glyphWidth;
            gi.Size.y = glyphHeight;
            gi.AtlasUV.x = entry.AtlasU0;
            gi.AtlasUV.y = entry.AtlasV0;
            gi.AtlasUV.z = entry.AtlasU1;
            gi.AtlasUV.w = entry.AtlasV1;
            m_CachedGlyphs.push_back(gi);
        }

        cursorX += entry.AdvanceWidth * m_Config.FontSize;
    }

    return Gem::Result::Success;
}

//================================================================================================
// CUIRectElement
//================================================================================================

void CUIRectElement::SetSize(const Math::FloatVector2& size)
{
    m_Size = size;
}

//------------------------------------------------------------------------------------------------
void CUIRectElement::SetFillColor(const Math::FloatVector4& color)
{
    m_FillColor = color;
}

} // namespace Canvas
