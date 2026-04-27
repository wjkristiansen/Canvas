//================================================================================================
// CUITextElement12 - Implementation
//================================================================================================

#include "pch.h"
#include "UITextElement12.h"

//------------------------------------------------------------------------------------------------
void CUITextElement12::SetText(PCSTR utf8Text)
{
    if (!utf8Text)
        utf8Text = "";

    if (m_Text == utf8Text)
        return;

    m_Text = utf8Text;
    m_Dirty = true;
}

//------------------------------------------------------------------------------------------------
void CUITextElement12::SetFont(Canvas::XFont* pFont)
{
    if (m_pFont == pFont)
        return;

    m_pFont = pFont;
    m_Dirty = true;
}

//------------------------------------------------------------------------------------------------
void CUITextElement12::SetLayoutConfig(const Canvas::TextLayoutConfig& config)
{
    bool geometryChanged = m_Config.FontSize != config.FontSize ||
                           m_Config.LineHeight != config.LineHeight ||
                           m_Config.DisableKerning != config.DisableKerning;

    m_Config = config;

    if (geometryChanged)
        m_Dirty = true;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CUITextElement12::Detach()
{
    m_pAttachedNode = nullptr;
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CUITextElement12::Update()
{
    if (!m_Dirty)
        return Gem::Result::Success;

    return RegenerateGlyphs();
}

//------------------------------------------------------------------------------------------------
Gem::Result CUITextElement12::RegenerateGlyphs()
{
    m_CachedGlyphs.clear();
    m_Dirty = false;

    if (m_Text.empty())
        return Gem::Result::Success;

    if (!m_pFont)
        return Gem::Result::BadPointer;
    if (!m_pGlyphCache)
        return Gem::Result::BadPointer;

    Canvas::CTrueTypeFont* pFontData = static_cast<Canvas::CFont*>(m_pFont)->GetFontData();
    if (!pFontData)
        return Gem::Result::BadPointer;

    std::vector<uint32_t> codepoints;
    Gem::Result result = Canvas::CTextLayout::DecodeUtf8(m_Text.c_str(), codepoints);
    if (Gem::Failed(result))
        return result;

    const Canvas::CTrueTypeFont::FontMetrics& metrics = pFontData->GetMetrics();
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
            Canvas::GlyphAtlasEntry spaceEntry = {};
            if (!m_pGlyphCache->CacheGlyphForFont(' ', *pFontData, spaceEntry))
                return Gem::Result::NotFound;
            cursorX += spaceEntry.AdvanceWidth * m_Config.FontSize * 4.0f;
            continue;
        }

        if (codepoint < 32)
            continue;

        Canvas::GlyphAtlasEntry entry = {};
        if (!m_pGlyphCache->CacheGlyphForFont(codepoint, *pFontData, entry))
            return Gem::Result::NotFound;

        float glyphWidth  = entry.BitmapWidth  * m_Config.FontSize;
        float glyphHeight = entry.BitmapHeight * m_Config.FontSize;

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
