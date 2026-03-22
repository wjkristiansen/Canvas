//================================================================================================
// CanvasText - Public API
//
// Core interfaces and types for text rendering via SDF-based glyphs.
//================================================================================================

#pragma once
#include "CanvasCore.h"
#include "CanvasGfx.h"

namespace Canvas
{

// Forward declarations for types defined in internal headers
struct GlyphOutline;        // Defined in Font.h
class CTrueTypeFont;        // Defined in Font.h

//------------------------------------------------------------------------------------------------
// XFont - Gem interface for font resources
//
// Wraps a TrueType font file loaded in memory.
// Provides glyph lookup and outline extraction.
//------------------------------------------------------------------------------------------------

struct XFont : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XFont, 0x7C3F5B8A2E1D9F06);
    
    // Get glyph index for Unicode codepoint
    GEMMETHOD_(uint16_t, GetGlyphIndex)(uint32_t codepoint) = 0;
    
    // Extract glyph outline by codepoint
    GEMMETHOD(GetGlyphOutline)(uint32_t codepoint, GlyphOutline &outline) = 0;
    
    // Get font metrics
    GEMMETHOD_(float, GetAscender)() = 0;
    GEMMETHOD_(float, GetDescender)() = 0;
    GEMMETHOD_(float, GetLineGap)() = 0;
    GEMMETHOD_(uint16_t, GetUnitsPerEm)() = 0;
    
    // Get underlying font object (for advanced use)
    GEMMETHOD_(CTrueTypeFont*, GetFontData)() = 0;
};

//------------------------------------------------------------------------------------------------
// XGlyphAtlas - Gem interface for glyph atlases
//
// Manages cached glyphs in a dynamic GPU texture atlas.
// Handles SDF generation, packing, and GPU uploads.
// 
// Note: Glyph caching is handled internally by QueueTextRender().
// Applications do not interact with caching directly.
//------------------------------------------------------------------------------------------------

struct XGlyphAtlas : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XGlyphAtlas, 0x9E2A6D7C1F4B8A3D);
    
    // Get GPU atlas texture
    GEMMETHOD_(XGfxSurface*, GetAtlasTexture)() = 0;
};

//------------------------------------------------------------------------------------------------
// Text Layout Configuration - used by QueueTextRender() API
//
// Specifies text rendering parameters.
//------------------------------------------------------------------------------------------------

struct TextLayoutConfig
{
    float FontSize;              // Size in pixels (or world units if rendering in 3D)
    uint32_t Color;              // RGBA packed as uint32 (default: white opaque)
    float LineHeight;            // Multiplier of font's line gap (default: 1.0)
    bool DisableKerning;         // Skip kerning adjustments (default: false)
    
    TextLayoutConfig()
        : FontSize(16.0f), Color(0xFFFFFFFF), LineHeight(1.0f), DisableKerning(false)
    {}
};

namespace TextFlags
{
    enum Enum : uint32_t
    {
        None            = 0,
        AlignLeft       = 0,
        AlignCenter     = 1 << 0,
        AlignRight      = 1 << 1,
        VertAlignTop    = 0,
        VertAlignCenter = 1 << 2,
        VertAlignBottom = 1 << 3,
    };
}

//------------------------------------------------------------------------------------------------
// Factory Functions
//
// Created as implementation classes (CFont, CGlyphAtlas).
// Use interface pointers for backend-agnostic access.
//------------------------------------------------------------------------------------------------

// Create font from TTF file data
Gem::Result CreateFontFromBuffer(
    XCanvas *pCanvas,
    const uint8_t *pTTFData,
    size_t dataSize,
    PCSTR pFontName,
    XFont **ppFont);

// Create glyph atlas with initial size
Gem::Result CreateGlyphAtlas(
    XCanvas *pCanvas,
    XGfxDevice *pGfxDevice,
    XGfxRenderQueue *pRenderQueue,
    uint32_t initialAtlasSize,
    XGlyphAtlas **ppAtlas);

//------------------------------------------------------------------------------------------------
// Queue text rendering to render queue
//
// High-level helper that combines layout + GPU buffer creation + render submission.
// Generates geometry for UTF-8 text string and queues to render queue as TextChunkData.
//
// Parameters:
//   pRenderQueue: Render queue to submit to
//   pGfxDevice: GPU device for buffer creation
//   utf8Text: UTF-8 encoded text string to render
//   pFont: Font to use for rendering
//   pGlyphAtlas: Glyph atlas with cached glyphs
//   screenPosition: Screen-space position (x, y, z=depth)
//   config: Text layout configuration (size, color, etc.)
//
// Returns: Success if text was queued, error code otherwise
//
// Note: The function handles UTF-8 decoding, glyph lookup, and GPU buffer management.
// Queue text rendering to render queue
//
// High-level helper that combines layout + GPU buffer creation + render submission.
// Generates geometry for UTF-8 text string and queues to render queue.
//
// Parameters:
//   pRenderQueue: Render queue to submit to
//   pGfxDevice: GPU device for buffer creation  
//   utf8Text: UTF-8 encoded text string to render
//   pFont: Font to use for rendering
//   pAtlas: Glyph atlas with cached glyphs
//   screenPosition: Screen-space position (x, y, z=depth)
//   config: Text layout configuration (size, color, etc.)
//
// Returns: Success if text was queued, error code otherwise
//
Gem::Result QueueTextRender(
    XGfxRenderQueue *pRenderQueue,
    XGfxDevice *pGfxDevice,
    PCSTR utf8Text,
    XFont *pFont,
    XGlyphAtlas *pAtlas,
    const Math::FloatVector3& screenPosition,
    const TextLayoutConfig& config,
    XLogger *pLogger = nullptr);

} // namespace Canvas
