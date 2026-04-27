//================================================================================================
// CanvasCore - Text Layout Engine
//
// Converts UTF-8 strings into positioned glyph geometry for rendering.
//================================================================================================

#pragma once
#include "Font.h"
#include "GlyphAtlas.h"
#include "CanvasCore.h"
#include <vector>
#include <cstdint>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Text Vertex - vertex format for standalone text layout
//
// Six vertices form one glyph quad (two triangles).
// Used by the standalone CTextLayout API and unit tests.
// GPU rendering uses GlyphInstance (HlslTypes.h) instead.
//------------------------------------------------------------------------------------------------

struct TextVertex
{
    Math::FloatVector3 Position;    // Screen-space pixel position (12 bytes)
    Math::FloatVector2 TexCoord;    // Atlas UV coordinates (8 bytes)
    float Color[4];                 // RGBA float color (16 bytes)

    TextVertex() : Color{1.0f, 1.0f, 1.0f, 1.0f} {}

    void SetColor(const Math::FloatVector4& c) { Color[0] = c.X; Color[1] = c.Y; Color[2] = c.Z; Color[3] = c.W; }
    void SetColor(float r, float g, float b, float a) { Color[0] = r; Color[1] = g; Color[2] = b; Color[3] = a; }
};

//------------------------------------------------------------------------------------------------
// Text Layout Engine
//
// Generates TextVertex data for text strings (standalone layout/measurement).
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
