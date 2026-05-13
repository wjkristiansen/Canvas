#pragma once
//================================================================================================
// GlyphCache - CPU-side glyph caching with SDF generation and rectangle packing
//
// GlyphAtlasEntry: cached glyph's atlas location and metrics
// PendingGlyphUpload: raw pixel data queued for GPU upload by the graphics subsystem
// CGlyphCache: CPU-side SDF generation, rectangle packing, glyph entry cache
//================================================================================================

#include "Font.h"
#include <unordered_map>
#include <memory>
#include <vector>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// GlyphAtlasEntry - describes a cached glyph in the atlas
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
// PendingGlyphUpload - raw pixel data queued for GPU upload
//
// Produced by CGlyphCache, consumed by the graphics subsystem (e.g. CanvasGfx12).
// Intentionally free of GPU types.
//------------------------------------------------------------------------------------------------

struct PendingGlyphUpload
{
    uint32_t PixelOffset;       // Offset into CGlyphCache's staging buffer
    uint32_t PixelSize;         // Size in bytes
    uint32_t Width;
    uint32_t Height;
    uint32_t AtlasX;
    uint32_t AtlasY;
    uint32_t BytesPerPixel;
};

//------------------------------------------------------------------------------------------------
// CGlyphCache - CPU-side glyph caching (no GPU dependency)
//------------------------------------------------------------------------------------------------

class CGlyphCache
{
public:
    CGlyphCache(uint32_t atlasSize = 512);
    ~CGlyphCache();

    bool GetCachedGlyph(uint32_t codepoint, GlyphAtlasEntry &outEntry) const;

    size_t GetCachedGlyphCount() const { return m_CachedGlyphs.size(); }
    uint32_t GetAtlasSize() const { return m_AtlasSize; }

    bool HasPendingUploads() const { return !m_PendingUploads.empty(); }
    std::vector<PendingGlyphUpload> TakePendingUploads() { return std::move(m_PendingUploads); }
    const uint8_t* GetStagingData() const { return m_StagingBuffer.data(); }
    void ClearStagingBuffer() { m_StagingBuffer.clear(); }

    bool CacheGlyphForFont(uint32_t codepoint, CTrueTypeFont &font, GlyphAtlasEntry &outEntry);

private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
    uint32_t m_AtlasSize;
    std::unordered_map<uint32_t, GlyphAtlasEntry> m_CachedGlyphs;
    std::vector<PendingGlyphUpload> m_PendingUploads;
    std::vector<uint8_t> m_StagingBuffer;
};

} // namespace Canvas
