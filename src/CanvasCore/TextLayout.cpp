//================================================================================================
// CanvasCore - Text Layout Engine Implementation
//================================================================================================

#include "pch.h"
#include "TextLayout.h"
#include "FontImpl.h"
#include "GlyphAtlas.h"
#include <cmath>
#include <algorithm>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// UTF-8 Decoding
//------------------------------------------------------------------------------------------------

Gem::Result CTextLayout::DecodeUtf8(PCSTR utf8Text, std::vector<uint32_t>& outCodepoints)
{
    if (!utf8Text)
        return Gem::Result::BadPointer;
    
    outCodepoints.clear();
    
    const uint8_t* pData = reinterpret_cast<const uint8_t*>(utf8Text);
    
    while (*pData != '\0')
    {
        uint32_t codepoint = 0;
        uint8_t lead = *pData++;
        
        if ((lead & 0x80) == 0)
        {
            codepoint = lead;
        }
        else if ((lead & 0xE0) == 0xC0)
        {
            if ((*pData & 0xC0) != 0x80) return Gem::Result::InvalidArg;
            codepoint = ((lead & 0x1F) << 6) | (*pData++ & 0x3F);
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            if ((*pData & 0xC0) != 0x80) return Gem::Result::InvalidArg;
            codepoint = ((lead & 0x0F) << 12) | ((*pData++ & 0x3F) << 6);
            if ((*pData & 0xC0) != 0x80) return Gem::Result::InvalidArg;
            codepoint |= (*pData++ & 0x3F);
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            if ((*pData & 0xC0) != 0x80) return Gem::Result::InvalidArg;
            codepoint = ((lead & 0x07) << 18) | ((*pData++ & 0x3F) << 12);
            if ((*pData & 0xC0) != 0x80) return Gem::Result::InvalidArg;
            codepoint |= (*pData++ & 0x3F) << 6;
            if ((*pData & 0xC0) != 0x80) return Gem::Result::InvalidArg;
            codepoint |= (*pData++ & 0x3F);
        }
        else
        {
            return Gem::Result::InvalidArg;
        }
        
        outCodepoints.push_back(codepoint);
    }
    
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
// Glyph Quad Generation
//------------------------------------------------------------------------------------------------

float CTextLayout::LayoutGlyph(
    uint32_t codepoint,
    const CTrueTypeFont& font,
    const GlyphAtlasEntry& atlasEntry,
    const Math::FloatVector3& position,
    uint32_t color,
    float fontSize,
    std::vector<TextVertex>& outVertices)
{
    (void)codepoint;
    (void)font;
    
    float glyphWidth  = atlasEntry.BitmapWidth  * fontSize;
    float glyphHeight = atlasEntry.BitmapHeight * fontSize;

    if (glyphWidth <= 0.0f || glyphHeight <= 0.0f)
        return atlasEntry.AdvanceWidth;

    float offsetX =  atlasEntry.LeftBearing * fontSize;
    float offsetY =  atlasEntry.TopBearing  * fontSize;
    
    float x0 = position.X + offsetX;
    float y0 = position.Y + offsetY;
    float x1 = x0 + glyphWidth;
    float y1 = y0 + glyphHeight;
    
    float u0 = atlasEntry.AtlasU0;
    float v0 = atlasEntry.AtlasV0;
    float u1 = atlasEntry.AtlasU1;
    float v1 = atlasEntry.AtlasV1;
    
    float z = position.Z;
    
    TextVertex v[6];
    
    v[0].Position = Math::FloatVector3(x0, y0, z);
    v[0].TexCoord = Math::FloatVector2(u0, v0);
    v[0].Color = color;
    
    v[1].Position = Math::FloatVector3(x0, y1, z);
    v[1].TexCoord = Math::FloatVector2(u0, v1);
    v[1].Color = color;
    
    v[2].Position = Math::FloatVector3(x1, y0, z);
    v[2].TexCoord = Math::FloatVector2(u1, v0);
    v[2].Color = color;
    
    v[3].Position = Math::FloatVector3(x0, y1, z);
    v[3].TexCoord = Math::FloatVector2(u0, v1);
    v[3].Color = color;
    
    v[4].Position = Math::FloatVector3(x1, y1, z);
    v[4].TexCoord = Math::FloatVector2(u1, v1);
    v[4].Color = color;
    
    v[5].Position = Math::FloatVector3(x1, y0, z);
    v[5].TexCoord = Math::FloatVector2(u1, v0);
    v[5].Color = color;
    
    for (int i = 0; i < 6; i++)
        outVertices.push_back(v[i]);
    
    return atlasEntry.AdvanceWidth;
}

//------------------------------------------------------------------------------------------------
// Main Layout Function
//------------------------------------------------------------------------------------------------

Gem::Result CTextLayout::GenerateGeometry(
    PCSTR utf8Text,
    const CTrueTypeFont& font,
    const TextLayoutConfig& config,
    const Math::FloatVector3& basePosition,
    std::vector<TextVertex>& outVertices)
{
    outVertices.clear();
    
    std::vector<uint32_t> codepoints;
    Gem::Result result = DecodeUtf8(utf8Text, codepoints);
    if (Gem::Failed(result))
        return result;
    
    const CTrueTypeFont::FontMetrics& metrics = font.GetMetrics();
    
    float lineHeightUnits = (metrics.Ascender - metrics.Descender + metrics.LineGap);
    float lineHeightPixels = lineHeightUnits * (config.FontSize / font.GetUnitsPerEm());
    
    Math::FloatVector3 cursorPos = basePosition;
    
    for (uint32_t codepoint : codepoints)
    {
        if (codepoint == '\n')
        {
            cursorPos.X = basePosition.X;
            cursorPos.Y += lineHeightPixels * config.LineHeight;
            continue;
        }
        
        if (codepoint == '\t')
        {
            codepoint = ' ';
            for (int i = 0; i < 4; i++)
            {
                // TODO: Get advance width from atlas entry or font metrics
            }
            continue;
        }
        
        if (codepoint < 32) // Skip control characters
            continue;
    }
    
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
// QueueTextRender - High-level helper that combines layout + GPU buffer creation + render submission.
//------------------------------------------------------------------------------------------------

Gem::Result CANVAS_API QueueTextRender(
    XGfxRenderQueue *pRenderQueue,
    XGfxDevice *pGfxDevice,
    PCSTR utf8Text,
    XFont *pFont,
    XGlyphAtlas *pAtlas,
    const Math::FloatVector3& screenPosition,
    const TextLayoutConfig& config,
    XLogger *pLogger)
{
    if (!pRenderQueue || !pGfxDevice || !utf8Text || !pFont || !pAtlas)
    {
        LogError(pLogger, "QueueTextRender: null argument (queue=%p device=%p text=%p font=%p atlas=%p)",
            pRenderQueue, pGfxDevice, utf8Text, pFont, pAtlas);
        return Gem::Result::BadPointer;
    }

    if (!utf8Text[0])
        return Gem::Result::Success;

    CGlyphAtlasImpl* pAtlasImpl = static_cast<CGlyphAtlasImpl*>(pAtlas);

    try
    {
        CFont* pFontImpl = static_cast<CFont*>(pFont);
        CTrueTypeFont* pFontData = pFontImpl->GetFontData();
        if (!pFontData)
        {
            LogError(pLogger, "QueueTextRender: font has no internal data");
            return Gem::Result::BadPointer;
        }

        std::vector<uint32_t> codepoints;
        Gem::Result result = CTextLayout::DecodeUtf8(utf8Text, codepoints);
        if (Gem::Failed(result))
        {
            LogError(pLogger, "QueueTextRender: UTF-8 decode failed (result=0x%08X)", (unsigned)result);
            return result;
        }

        std::vector<GlyphAtlasEntry> glyphEntries;
        glyphEntries.reserve(codepoints.size());
        Math::FloatVector3 cursorPos = screenPosition;
        const CTrueTypeFont::FontMetrics& metrics = pFontData->GetMetrics();
        float lineHeightUnits = (metrics.Ascender - metrics.Descender + metrics.LineGap);
        float lineHeightPixels = lineHeightUnits * (config.FontSize / pFontData->GetUnitsPerEm());

        for (uint32_t codepoint : codepoints)
        {
            if (codepoint == '\n')
            {
                cursorPos.X = screenPosition.X;
                cursorPos.Y += lineHeightPixels * config.LineHeight;
                continue;
            }

            if (codepoint == '\t')
            {
                codepoint = ' ';
                for (int i = 0; i < 4; i++)
                {
                    GlyphAtlasEntry entry = {};
                    result = pAtlasImpl->InternalCacheGlyph(codepoint, pFont, entry);
                    if (Gem::Failed(result))
                    {
                        LogError(pLogger, "QueueTextRender: CacheGlyph failed for tab-space (result=0x%08X)", (unsigned)result);
                        return result;
                    }
                    cursorPos.X += entry.AdvanceWidth * config.FontSize;
                }
                continue;
            }

            if (codepoint < 32)
                continue;

            GlyphAtlasEntry entry = {};
            result = pAtlasImpl->InternalCacheGlyph(codepoint, pFont, entry);
            if (Gem::Failed(result))
            {
                LogError(pLogger, "QueueTextRender: CacheGlyph failed for codepoint U+%04X (result=0x%08X)", codepoint, (unsigned)result);
                return result;
            }

            glyphEntries.push_back(entry);
        }

        std::vector<TextVertex> vertices;
        vertices.reserve(glyphEntries.size() * 6);
        cursorPos = screenPosition;

        size_t glyphIdx = 0;
        for (uint32_t codepoint : codepoints)
        {
            if (codepoint == '\n')
            {
                cursorPos.X = screenPosition.X;
                cursorPos.Y += lineHeightPixels * config.LineHeight;
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
                config.Color,
                config.FontSize,
                vertices);

            cursorPos.X += advance * config.FontSize;
            glyphIdx++;
        }

        if (vertices.empty())
        {
            LogWarn(pLogger, "QueueTextRender: no renderable glyphs produced for text \"%s\"", utf8Text);
            return Gem::Result::Success;
        }

        XGfxSurface* pAtlasTexture = pAtlas->GetAtlasTexture();
        if (!pAtlasTexture)
        {
            LogError(pLogger, "QueueTextRender: atlas texture is null");
            return Gem::Result::InvalidArg;
        }

        result = pRenderQueue->DrawText(vertices.data(), static_cast<uint32_t>(vertices.size()),
            pAtlasTexture, Math::FloatVector4(screenPosition.X, screenPosition.Y, screenPosition.Z, 0.0f));
        if (Gem::Failed(result))
            LogError(pLogger, "QueueTextRender: DrawText failed (result=0x%08X)", (unsigned)result);

        return result;
    }
    catch (std::bad_alloc)
    {
        LogError(pLogger, "QueueTextRender: out of memory");
        return Gem::Result::OutOfMemory;
    }
    catch (Gem::GemError &e)
    {
        LogError(pLogger, "QueueTextRender: exception (result=0x%08X)", (unsigned)e.Result());
        return e.Result();
    }
}

} // namespace Canvas
