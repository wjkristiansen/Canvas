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
// Layout: Position (12 bytes) + TexCoord (8 bytes) + Color (4 bytes) = 24 bytes total
//------------------------------------------------------------------------------------------------

struct TextVertex
{
    Math::FloatVector3 Position;    // 3D screen/world position (12 bytes)
    Math::FloatVector2 TexCoord;    // Atlas UV coordinates (8 bytes)
    uint32_t Color;                 // RGBA packed as uint32 (4 bytes)
    
    TextVertex() : Color(0xFFFFFFFF) {}
};

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
        uint32_t color,
        float fontSize,
        std::vector<TextVertex>& outVertices
    );
};

} // namespace Canvas
