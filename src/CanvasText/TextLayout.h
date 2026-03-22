//================================================================================================
// CanvasText - Text Layout Engine
//
// Converts UTF-8 strings into positioned glyph geometry for rendering.
// Handles font metrics, kerning, and glyph quad generation.
//================================================================================================

#pragma once
#include "Font.h"
#include "SDFGenerator.h"
#include "CanvasText.h"     // For TextLayoutConfig (public types)
#include "GlyphAtlas.h"     // For GlyphAtlasEntry (internal type)
#include "CanvasMath.hpp"
#include <vector>
#include <cstdint>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Text Vertex - Internal vertex format for text rendering
//
// Used only internally within CanvasText for vertex generation.
// Represents a single vertex for text mesh rendering.
// Six vertices form one glyph quad (two triangles).
//
// NOT part of the public API - clients use QueueTextRender() instead.
//
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
//
// Vertex format: TextVertex (internal)
//   - Position: screen/world space XYZ
//   - TexCoord: atlas UV coordinates (0-1 range)
//   - Color: packed RGBA
//
// Layout config: TextLayoutConfig (from public CanvasText.h)
//   - FontSize, Color, LineHeight, DisableKerning
//
// Note: TextLayoutConfig is used but defined in CanvasText.h (public API)
//------------------------------------------------------------------------------------------------

class CTextLayout
{
public:
    // Generate geometry from UTF-8 string
    // Output vertices ready for GPU upload
    // basePosition: screen-space (x, y) + optional z for depth
    Gem::Result GenerateGeometry(
        PCSTR utf8Text,                         // Input: UTF-8 encoded text
        const CTrueTypeFont& font,              // Input: font metrics and data
        const TextLayoutConfig& config,         // Input: layout configuration
        const Math::FloatVector3& basePosition, // Input: top-left position
        std::vector<TextVertex>& outVertices    // Output: vertex data for 2 triangles per glyph
    );

    // Convert UTF-8 string to Unicode codepoints
    static Gem::Result DecodeUtf8(
        PCSTR utf8Text,
        std::vector<uint32_t>& outCodepoints
    );
    
    // Generate quad (6 vertices) for a single glyph
    // Returns the advance width (em fraction) for positioning the next glyph
    // fontSize scales em-fraction metrics (BitmapWidth/Height, bearings) to screen pixels
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
