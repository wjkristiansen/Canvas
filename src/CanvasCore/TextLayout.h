//================================================================================================
// CanvasCore - Text Layout Engine
//
// Converts UTF-8 strings into positioned glyph geometry for rendering.
//================================================================================================

#pragma once
#include "Font.h"
#include "GlyphAtlas.h"
#include <vector>
#include <cstdint>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Text Vertex - vertex format for text rendering
//
// Six vertices form one glyph quad (two triangles).
// TextVertex is defined in CanvasCore.h (shared with CanvasGfx12)

//------------------------------------------------------------------------------------------------
// Text Layout Engine
//
// Generates vertex data for text strings.
// One quad (6 vertices) per glyph.
//------------------------------------------------------------------------------------------------

class CANVAS_API CTextLayout
{
public:
    // Generate geometry from UTF-8 string
    Gem::Result GenerateGeometry(
        PCSTR utf8Text,
        const CTrueTypeFont& font,
        const TextLayoutConfig& config,
        const Math::FloatVector3& basePosition,
        std::vector<TextVertex>& outVertices
    );

    // Convert UTF-8 string to Unicode codepoints
    static Gem::Result DecodeUtf8(
        PCSTR utf8Text,
        std::vector<uint32_t>& outCodepoints
    );
    
    // Generate quad (6 vertices) for a single glyph
    static float LayoutGlyph(
        uint32_t codepoint,
        const CTrueTypeFont& font,
        const GlyphAtlasEntry& atlasEntry,
        const Math::FloatVector3& position,
        const Math::FloatVector4& color,
        float fontSize,
        std::vector<TextVertex>& outVertices
    );
};

} // namespace Canvas
