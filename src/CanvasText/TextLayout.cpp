//================================================================================================
// CanvasText - Text Layout Engine Implementation
//================================================================================================

#include "TextLayout.h"
#include "CanvasText.h"
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
            // Single-byte character (0xxxxxxx)
            codepoint = lead;
        }
        else if ((lead & 0xE0) == 0xC0)
        {
            // Two-byte character (110xxxxx 10xxxxxx)
            if ((*pData & 0xC0) != 0x80) return Gem::Result::InvalidArg;
            codepoint = ((lead & 0x1F) << 6) | (*pData++ & 0x3F);
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            // Three-byte character (1110xxxx 10xxxxxx 10xxxxxx)
            if ((*pData & 0xC0) != 0x80) return Gem::Result::InvalidArg;
            codepoint = ((lead & 0x0F) << 12) | ((*pData++ & 0x3F) << 6);
            if ((*pData & 0xC0) != 0x80) return Gem::Result::InvalidArg;
            codepoint |= (*pData++ & 0x3F);
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            // Four-byte character (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
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
//
// Creates 6 vertices for a textured quad (2 triangles).
// Positions are in screen space, UVs cover the glyph's atlas region.
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
    (void)codepoint;  // Reserved for future validation/metadata
    (void)font;       // Reserved for glyph metrics/kerning
    
    // Glyph dimensions in screen pixels (em fractions × fontSize)
    float glyphWidth  = atlasEntry.BitmapWidth  * fontSize;
    float glyphHeight = atlasEntry.BitmapHeight * fontSize;

    // Whitespace / outline-less glyphs have no bitmap — skip quad, just return advance
    if (glyphWidth <= 0.0f || glyphHeight <= 0.0f)
        return atlasEntry.AdvanceWidth;

    // Position offset from line top (em fractions × fontSize → screen pixels).
    // TopBearing is the gap from the ascender line down to the glyph ink top;
    // positive offsetY moves the quad downward from the line-top position.
    float offsetX =  atlasEntry.LeftBearing * fontSize;
    float offsetY =  atlasEntry.TopBearing  * fontSize;  // Down from line top to glyph ink top
    
    // Screen position (top-left of glyph quad)
    float x0 = position.X + offsetX;
    float y0 = position.Y + offsetY;
    float x1 = x0 + glyphWidth;
    float y1 = y0 + glyphHeight;
    
    // Atlas UVs
    float u0 = atlasEntry.AtlasU0;
    float v0 = atlasEntry.AtlasV0;
    float u1 = atlasEntry.AtlasU1;
    float v1 = atlasEntry.AtlasV1;
    
    float z = position.Z;
    
    // Create vertices for 2 triangles (6 vertices total)
    TextVertex v[6];
    
    // Triangle 1: top-left, bottom-left, top-right
    v[0].Position = Math::FloatVector3(x0, y0, z);
    v[0].TexCoord = Math::FloatVector2(u0, v0);
    v[0].Color = color;
    
    v[1].Position = Math::FloatVector3(x0, y1, z);
    v[1].TexCoord = Math::FloatVector2(u0, v1);
    v[1].Color = color;
    
    v[2].Position = Math::FloatVector3(x1, y0, z);
    v[2].TexCoord = Math::FloatVector2(u1, v0);
    v[2].Color = color;
    
    // Triangle 2: bottom-left, bottom-right, top-right
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
    
    // Return advance width for positioning next glyph
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
    
    // Decode UTF-8 to codepoints
    std::vector<uint32_t> codepoints;
    Gem::Result result = DecodeUtf8(utf8Text, codepoints);
    if (Gem::Failed(result))
        return result;
    
    // Get font metrics
    const CTrueTypeFont::FontMetrics& metrics = font.GetMetrics();
    
    // Calculate line height and scale
    float lineHeightUnits = (metrics.Ascender - metrics.Descender + metrics.LineGap);
    float lineHeightPixels = lineHeightUnits * (config.FontSize / font.GetUnitsPerEm());
    
    // Current cursor position in screen space
    Math::FloatVector3 cursorPos = basePosition;
    
    // Layout each codepoint
    for (uint32_t codepoint : codepoints)
    {
        // Handle special characters
        if (codepoint == '\n')
        {
            cursorPos.X = basePosition.X;
            cursorPos.Y += lineHeightPixels * config.LineHeight;
            continue;
        }
        
        if (codepoint == '\t')
        {
            codepoint = ' ';  // Tab = 4 spaces (simplified)
            for (int i = 0; i < 4; i++)
            {
                // TODO: Get advance width from atlas entry or font metrics
                // For now, stub out this path since we need the atlas to cache
                (void)font;  // font not yet used in simplified tab handling
                cursorPos.X += 10.0f;  // Placeholder advance
            }
            continue;
        }
        
        if (codepoint < 32)  // Skip other control characters
            continue;
        
        // Note: In a complete implementation, we'd:
        // 1. Call pAtlas->CacheGlyph(codepoint, ...) to ensure it's in the atlas
        // 2. Call pAtlas->GetCachedGlyph(codepoint, ...) to get the entry
        // For now, this returns GlyphAtlasEntry that would be provided externally
        
        // This is a stub - actual implementation needs the atlas:
        // GlyphAtlasEntry atlasEntry = ...;
        // float advance = LayoutGlyph(codepoint, font, atlasEntry, cursorPos, config.Color, outVertices);
        // cursorPos.x += advance;
    }
    
    return Gem::Result::Success;
}

} // namespace Canvas
