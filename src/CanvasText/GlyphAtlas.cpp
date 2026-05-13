//================================================================================================
// GlyphCache - Implementation
//================================================================================================

#include "pch.h"
#include "GlyphAtlas.h"
#include "SDFGenerator.h"
#include "RectanglePacker.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
struct CGlyphCache::Impl
{
    CSDFGenerator::Config SDFConfig;
    std::unique_ptr<CRectanglePacker> pPacker;
    SDFBitmap StagingBitmap;    // Reused across CacheGlyphForFont calls

    Impl(uint32_t atlasSize)
        : pPacker(std::make_unique<CRectanglePacker>(atlasSize, atlasSize))
    {
        SDFConfig.TextureSize = 64;
        SDFConfig.SampleRange = 0.1f;
        SDFConfig.GenerateMSDF = false;
    }
};

//------------------------------------------------------------------------------------------------
CGlyphCache::CGlyphCache(uint32_t atlasSize)
    : m_pImpl(std::make_unique<Impl>(atlasSize))
    , m_AtlasSize(atlasSize)
{
}

CGlyphCache::~CGlyphCache() = default;

bool CGlyphCache::GetCachedGlyph(uint32_t codepoint, GlyphAtlasEntry &outEntry) const
{
    auto it = m_CachedGlyphs.find(codepoint);
    if (it != m_CachedGlyphs.end())
    {
        outEntry = it->second;
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------------------------
bool CGlyphCache::CacheGlyphForFont(uint32_t codepoint, CTrueTypeFont &font, GlyphAtlasEntry &outEntry)
{
    if (GetCachedGlyph(codepoint, outEntry))
        return true;

    uint16_t glyphIndex = font.GetGlyphIndex(codepoint);

    GlyphOutline outline;
    if (!font.GetGlyphOutline(glyphIndex, outline))
        return false;

    SDFBitmap& sdfBitmap = m_pImpl->StagingBitmap;
    CSDFGenerator generator;
    if (!generator.Generate(outline, font, m_pImpl->SDFConfig, sdfBitmap))
    {
        outEntry.Codepoint   = codepoint;
        outEntry.GlyphIndex  = glyphIndex;
        outEntry.AdvanceWidth = font.NormalizeX(outline.AdvanceWidth);
        outEntry.LeftBearing  = 0.0f;
        outEntry.TopBearing   = 0.0f;
        outEntry.BitmapWidth  = 0.0f;
        outEntry.BitmapHeight = 0.0f;
        outEntry.AtlasU0 = outEntry.AtlasV0 = 0.0f;
        outEntry.AtlasU1 = outEntry.AtlasV1 = 0.0f;
        m_CachedGlyphs[codepoint] = outEntry;
        return true;
    }

    uint32_t bitmapWidth = m_pImpl->SDFConfig.TextureSize;
    uint32_t bitmapHeight = m_pImpl->SDFConfig.TextureSize;

    CRectanglePacker::Rect atlasRect;
    if (!m_pImpl->pPacker->Allocate(bitmapWidth, bitmapHeight, atlasRect))
        return false;

    outEntry.Codepoint = codepoint;
    outEntry.GlyphIndex = glyphIndex;
    outEntry.AdvanceWidth = sdfBitmap.AdvanceWidth;
    outEntry.LeftBearing = sdfBitmap.MinX;
    float ascender = font.NormalizeY(font.GetMetrics().Ascender);
    outEntry.TopBearing  = ascender - sdfBitmap.MaxY;
    outEntry.BitmapWidth  = sdfBitmap.MaxX - sdfBitmap.MinX;
    outEntry.BitmapHeight = sdfBitmap.MaxY - sdfBitmap.MinY;

    outEntry.AtlasU0 = static_cast<float>(atlasRect.X) / m_AtlasSize;
    outEntry.AtlasV0 = static_cast<float>(atlasRect.Y) / m_AtlasSize;
    outEntry.AtlasU1 = static_cast<float>(atlasRect.X + bitmapWidth)  / m_AtlasSize;
    outEntry.AtlasV1 = static_cast<float>(atlasRect.Y + bitmapHeight) / m_AtlasSize;

    PendingGlyphUpload upload;
    upload.Width = bitmapWidth;
    upload.Height = bitmapHeight;
    upload.AtlasX = atlasRect.X;
    upload.AtlasY = atlasRect.Y;
    upload.BytesPerPixel = sdfBitmap.BytesPerPixel;
    upload.PixelOffset = static_cast<uint32_t>(m_StagingBuffer.size());
    upload.PixelSize = static_cast<uint32_t>(sdfBitmap.Data.size());
    m_StagingBuffer.insert(m_StagingBuffer.end(), sdfBitmap.Data.begin(), sdfBitmap.Data.end());
    m_PendingUploads.push_back(upload);

    m_CachedGlyphs[codepoint] = outEntry;
    return true;
}

} // namespace Canvas
