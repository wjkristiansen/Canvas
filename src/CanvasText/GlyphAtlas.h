#pragma once
//================================================================================================
// CanvasText - Glyph Atlas Manager
//
// Manages a dynamic texture atlas for cached glyphs.
// Combines CPU-side rectangle packing with GPU texture management through XGfx* interfaces.
//
// Architecture:
// 1. CPU: Rectangle bin packing allocator
// 2. GPU: Texture created via XGfxDevice
// 3. Upload: GpuTask system schedules copies
//================================================================================================

#include "SDFGenerator.h"
#include "CanvasGfx.h"
#include <unordered_map>
#include <memory>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Glyph Atlas Entry - describes a cached glyph in the atlas
//
// Internal type used by CGlyphAtlas for managing cached glyphs.
// NOT exposed in public CanvasText.h API - clients use QueueTextRender() instead.
//------------------------------------------------------------------------------------------------

struct GlyphAtlasEntry
{
    // Glyph identification
    uint32_t Codepoint;        // Unicode codepoint
    uint16_t GlyphIndex;       // Index in font
    
    // Atlas location (texture coordinates in [0,1])
    float AtlasU0, AtlasV0;
    float AtlasU1, AtlasV1;
    
    // Glyph metrics (in normalized space, e.g., 0=baseline, 1=1em height)
    float AdvanceWidth;
    float LeftBearing;
    float TopBearing;          // Distance (em) from line top (ascender) down to glyph ink top; always >= 0
    float BitmapWidth;         // Width of SDF bitmap in atlas pixels
    float BitmapHeight;        // Height of SDF bitmap in atlas pixels
    
    GlyphAtlasEntry()
        : Codepoint(0), GlyphIndex(0)
        , AtlasU0(0), AtlasV0(0), AtlasU1(0), AtlasV1(0)
        , AdvanceWidth(0), LeftBearing(0), TopBearing(0)
        , BitmapWidth(0), BitmapHeight(0)
    {}
};

//------------------------------------------------------------------------------------------------
// Rectangle Packer - simple power-of-two bin packing
//------------------------------------------------------------------------------------------------

class CRectanglePacker
{
public:
    struct Rect
    {
        uint32_t X, Y, Width, Height;
        Rect() : X(0), Y(0), Width(0), Height(0) {}
        Rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) : X(x), Y(y), Width(w), Height(h) {}
    };
    
    CRectanglePacker(uint32_t textureWidth, uint32_t textureHeight);
    
    // Try to allocate a rectangle
    // Returns true if successful, false if no space available
    bool Allocate(uint32_t width, uint32_t height, Rect &outRect);
    
    // Get remaining free area (for reallocation checks)
    uint32_t GetFreeArea() const;
    uint32_t GetTotalArea() const { return m_TextureWidth * m_TextureHeight; }
    
private:
    uint32_t m_TextureWidth, m_TextureHeight;
    std::vector<Rect> m_AllocatedRects;
    
    // Check if rectangle overlaps any allocated rect
    bool CanAllocate(const Rect &candidate) const;
};

//------------------------------------------------------------------------------------------------
// Glyph Atlas - manages cached glyphs in a texture
//
// Responsibilities:
// - CPU: Pack glyph SDFs into rectaangle layout
// - GPU: Create/maintain atlas texture via XGfxDevice
// - Transfer: Queue GpuTasks to upload SDF data
// - Lookup: O(1) glyph location by codepoint
//
// Design: Single large texture atlas that grows dynamically.
// When full, new atlas is created and old one is retired.
//------------------------------------------------------------------------------------------------

class CGlyphAtlas
{
public:
    CGlyphAtlas(uint32_t initialAtlasSize = 512);
    ~CGlyphAtlas();
    
    // Initialize with GPU device
    // Must be called before using atlas
    Gem::Result Initialize(XGfxDevice *pDevice, XGfxRenderQueue *pRenderQueue);
    
    // Request glyph to be in atlas
    // If already cached, returns existing entry immediately
    // If not cached, generates SDF and schedules GPU upload
    Gem::Result CacheGlyph(uint32_t codepoint, const CTrueTypeFont &font,
                          const CSDFGenerator::Config &sdfConfig,
                          GlyphAtlasEntry &outEntry);
    
    // Get cached glyph entry (returns false if not in cache)
    bool GetCachedGlyph(uint32_t codepoint, GlyphAtlasEntry &outEntry) const;
    
    // Get GPU atlas texture
    XGfxSurface* GetAtlasTexture() const { return m_pAtlasTexture.Get(); }
    
    // Debug: get number of cached glyphs
    size_t GetCachedGlyphCount() const { return m_CachedGlyphs.size(); }

private:
    // GPU device and queue (held as weak references)
    XGfxDevice *m_pDevice;
    XGfxRenderQueue *m_pRenderQueue;
    
    // GPU atlas texture and its current size
    Gem::TGemPtr<XGfxSurface> m_pAtlasTexture;
    uint32_t m_AtlasSize;
    
    // CPU-side packing allocator
    std::unique_ptr<CRectanglePacker> m_pPacker;
    
    // Glyph cache: codepoint -> GlyphAtlasEntry
    std::unordered_map<uint32_t, GlyphAtlasEntry> m_CachedGlyphs;
    
    // Create new atlas texture
    Gem::Result CreateAtlasTexture(uint32_t size);
};

} // namespace Canvas
