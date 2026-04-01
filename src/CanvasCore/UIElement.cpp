//================================================================================================
// UIElement - Implementation
//================================================================================================

#include "pch.h"
#include "UIElement.h"
#include "FontImpl.h"

namespace Canvas
{

//================================================================================================
// CUIElementCore
//================================================================================================

CUIElementCore::CUIElementCore()
{
}

CUIElementCore::~CUIElementCore()
{
    // Free all child wrapper nodes (drops refs on children)
    ChildNode* pChild = m_pFirstChild;
    while (pChild)
    {
        CUIElementCore* pChildCore = GetCore(pChild->pElement);
        if (pChildCore)
            pChildCore->m_pParent = nullptr;
        ChildNode* pNext = pChild->pNext;
        delete pChild;
        pChild = pNext;
    }
    m_pFirstChild = nullptr;

    RemoveFromParent();
}

//------------------------------------------------------------------------------------------------
CUIElementCore* CUIElementCore::GetCore(XUIElement* pElement)
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
CUIElementCore::ChildNode* CUIElementCore::FindChildNode(XUIElement* pChild)
{
    for (ChildNode* pNode = m_pFirstChild; pNode; pNode = pNode->pNext)
    {
        if (pNode->pElement.Get() == pChild)
            return pNode;
    }
    return nullptr;
}

//------------------------------------------------------------------------------------------------
void CUIElementCore::AddChild(XUIElement* pChild)
{
    if (!pChild)
        return;

    CUIElementCore* pChildCore = GetCore(pChild);
    if (!pChildCore || pChildCore->m_pParent == this)
        return;

    // Remove from old parent if any
    pChildCore->RemoveFromParent();
    pChildCore->m_pParent = this;

    // Create wrapper node (AddRefs via TGemPtr)
    ChildNode* pNode = new ChildNode();
    pNode->pElement = pChild;

    if (!m_pFirstChild)
    {
        m_pFirstChild = pNode;
    }
    else
    {
        ChildNode* pLast = m_pFirstChild;
        while (pLast->pNext)
            pLast = pLast->pNext;
        pLast->pNext = pNode;
        pNode->pPrev = pLast;
    }

    pChildCore->InvalidatePosition();
}

//------------------------------------------------------------------------------------------------
void CUIElementCore::RemoveChild(XUIElement* pChild)
{
    if (!pChild)
        return;

    ChildNode* pNode = FindChildNode(pChild);
    if (!pNode)
        return;

    CUIElementCore* pChildCore = GetCore(pChild);
    if (pChildCore)
        pChildCore->m_pParent = nullptr;

    if (pNode->pPrev)
        pNode->pPrev->pNext = pNode->pNext;
    else
        m_pFirstChild = pNode->pNext;

    if (pNode->pNext)
        pNode->pNext->pPrev = pNode->pPrev;

    delete pNode;  // drops TGemPtr ref
}

//------------------------------------------------------------------------------------------------
void CUIElementCore::RemoveFromParent()
{
    if (m_pParent)
        m_pParent->RemoveChild(GetInterface());
}

//------------------------------------------------------------------------------------------------
CUIElementCore* CUIElementCore::GetFirstChildCore()
{
    return m_pFirstChild ? GetCore(m_pFirstChild->pElement) : nullptr;
}

//------------------------------------------------------------------------------------------------
CUIElementCore* CUIElementCore::GetNextSiblingCore()
{
    if (!m_pParent)
        return nullptr;

    // Find our wrapper node in parent's child list, then return the next
    ChildNode* pNode = m_pParent->FindChildNode(GetInterface());
    if (pNode && pNode->pNext)
        return GetCore(pNode->pNext->pElement);
    return nullptr;
}

//------------------------------------------------------------------------------------------------
void CUIElementCore::SetPosition(const Math::FloatVector2& position)
{
    if (m_Position.X == position.X && m_Position.Y == position.Y)
        return;

    m_Position = position;
    InvalidatePosition();
}

//------------------------------------------------------------------------------------------------
void CUIElementCore::SetVisible(bool visible)
{
    if (m_Visible == visible)
        return;

    m_Visible = visible;
    InvalidateVisibility();
}

//------------------------------------------------------------------------------------------------
bool CUIElementCore::IsEffectivelyVisible() const
{
    if (!m_Visible)
        return false;
    if (m_pParent)
        return m_pParent->IsEffectivelyVisible();
    return true;
}

//------------------------------------------------------------------------------------------------
const Math::FloatVector2& CUIElementCore::GetAbsolutePosition()
{
    if (m_DirtyFlags & DirtyPosition)
        RecomputeAbsolutePosition();
    return m_AbsolutePosition;
}

//------------------------------------------------------------------------------------------------
void CUIElementCore::InvalidatePosition()
{
    if (m_DirtyFlags & DirtyPosition)
        return;

    m_DirtyFlags |= DirtyPosition;

    for (ChildNode* pChild = m_pFirstChild; pChild; pChild = pChild->pNext)
    {
        CUIElementCore* pCore = GetCore(pChild->pElement);
        if (pCore)
            pCore->InvalidatePosition();
    }
}

//------------------------------------------------------------------------------------------------
void CUIElementCore::InvalidateVisibility()
{
    m_DirtyFlags |= DirtyVisibility;

    for (ChildNode* pChild = m_pFirstChild; pChild; pChild = pChild->pNext)
    {
        CUIElementCore* pCore = GetCore(pChild->pElement);
        if (pCore)
            pCore->InvalidateVisibility();
    }
}

//------------------------------------------------------------------------------------------------
void CUIElementCore::RecomputeAbsolutePosition()
{
    if (m_pParent)
    {
        const auto& parentAbs = m_pParent->GetAbsolutePosition();
        m_AbsolutePosition.X = parentAbs.X + m_Position.X;
        m_AbsolutePosition.Y = parentAbs.Y + m_Position.Y;
    }
    else
    {
        m_AbsolutePosition = m_Position;
    }

    m_DirtyFlags &= ~DirtyPosition;
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
    m_DirtyFlags |= DirtyContent;
}

//------------------------------------------------------------------------------------------------
void CUITextElement::SetFont(XFont* pFont)
{
    if (m_pFont == pFont)
        return;

    m_pFont = pFont;
    m_DirtyFlags |= DirtyContent;
}

//------------------------------------------------------------------------------------------------
void CUITextElement::SetGlyphAtlasInternal(CGlyphAtlasImpl* pAtlas)
{
    if (m_pAtlas == pAtlas)
        return;

    m_pAtlas = pAtlas;
    m_DirtyFlags |= DirtyContent;
}

//------------------------------------------------------------------------------------------------
void CUITextElement::SetLayoutConfig(const TextLayoutConfig& config)
{
    if (m_Config.FontSize == config.FontSize &&
        m_Config.Color == config.Color &&
        m_Config.LineHeight == config.LineHeight &&
        m_Config.DisableKerning == config.DisableKerning)
        return;

    m_Config = config;
    m_DirtyFlags |= DirtyContent;
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

    const Math::FloatVector2& absPos = GetAbsolutePosition();
    Math::FloatVector3 screenPos(absPos.X, absPos.Y, 0.0f);

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

    m_BufferSlot.GpuDirty = true;
}

//================================================================================================
// CUIRectElement
//================================================================================================

void CUIRectElement::SetSize(const Math::FloatVector2& size)
{
    if (m_Size.X == size.X && m_Size.Y == size.Y)
        return;

    m_Size = size;
    m_DirtyFlags |= DirtyContent;
}

//------------------------------------------------------------------------------------------------
void CUIRectElement::SetFillColor(uint32_t color)
{
    if (m_FillColor == color)
        return;

    m_FillColor = color;
    m_DirtyFlags |= DirtyContent;
}

//------------------------------------------------------------------------------------------------
void CUIRectElement::RegenerateVertices()
{
    m_CachedVertices.clear();

    if (m_Size.X <= 0.0f || m_Size.Y <= 0.0f)
        return;

    const Math::FloatVector2& absPos = GetAbsolutePosition();
    float x0 = absPos.X;
    float y0 = absPos.Y;
    float x1 = x0 + m_Size.X;
    float y1 = y0 + m_Size.Y;

    m_CachedVertices.resize(6);

    auto makeVertex = [](float x, float y, uint32_t color) -> TextVertex
    {
        TextVertex v;
        v.Position = { x, y, 0.0f };
        v.TexCoord = {};
        v.Color = color;
        return v;
    };

    m_CachedVertices[0] = makeVertex(x0, y0, m_FillColor);
    m_CachedVertices[1] = makeVertex(x0, y1, m_FillColor);
    m_CachedVertices[2] = makeVertex(x1, y0, m_FillColor);
    m_CachedVertices[3] = makeVertex(x0, y1, m_FillColor);
    m_CachedVertices[4] = makeVertex(x1, y1, m_FillColor);
    m_CachedVertices[5] = makeVertex(x1, y0, m_FillColor);

    m_BufferSlot.GpuDirty = true;
}

} // namespace Canvas
