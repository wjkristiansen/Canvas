//================================================================================================
// GlyphCache - Implementation
//================================================================================================

#include "pch.h"
#include "GlyphAtlas.h"
#include "FontImpl.h"
#include "SDFGenerator.h"
#include "RectanglePacker.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
struct CGlyphCache::Impl
{
    CSDFGenerator::Config SDFConfig;
    std::unique_ptr<CRectanglePacker> pPacker;

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
Gem::Result CGlyphCache::CacheGlyphForFont(uint32_t codepoint, XFont *pFont, GlyphAtlasEntry &outEntry)
{
    if (!pFont)
        return Gem::Result::BadPointer;

    if (GetCachedGlyph(codepoint, outEntry))
        return Gem::Result::Success;

    CFont *pFontImpl = static_cast<CFont*>(pFont);
    CTrueTypeFont *pFontData = pFontImpl->GetFontData();
    if (!pFontData)
        return Gem::Result::BadPointer;

    uint16_t glyphIndex = pFontData->GetGlyphIndex(codepoint);

    GlyphOutline outline;
    if (!pFontData->GetGlyphOutline(glyphIndex, outline))
        return Gem::Result::NotFound;

    SDFBitmap sdfBitmap;
    CSDFGenerator generator;
    if (!generator.Generate(outline, *pFontData, m_pImpl->SDFConfig, sdfBitmap))
    {
        outEntry.Codepoint   = codepoint;
        outEntry.GlyphIndex  = glyphIndex;
        outEntry.AdvanceWidth = pFontData->NormalizeX(outline.AdvanceWidth);
        outEntry.LeftBearing  = 0.0f;
        outEntry.TopBearing   = 0.0f;
        outEntry.BitmapWidth  = 0.0f;
        outEntry.BitmapHeight = 0.0f;
        outEntry.AtlasU0 = outEntry.AtlasV0 = 0.0f;
        outEntry.AtlasU1 = outEntry.AtlasV1 = 0.0f;
        m_CachedGlyphs[codepoint] = outEntry;
        return Gem::Result::Success;
    }

    uint32_t bitmapWidth = m_pImpl->SDFConfig.TextureSize;
    uint32_t bitmapHeight = m_pImpl->SDFConfig.TextureSize;

    CRectanglePacker::Rect atlasRect;
    if (!m_pImpl->pPacker->Allocate(bitmapWidth, bitmapHeight, atlasRect))
        return Gem::Result::OutOfMemory;

    outEntry.Codepoint = codepoint;
    outEntry.GlyphIndex = glyphIndex;
    outEntry.AdvanceWidth = sdfBitmap.AdvanceWidth;
    outEntry.LeftBearing = sdfBitmap.MinX;
    float ascender = pFontData->NormalizeY(pFontData->GetMetrics().Ascender);
    outEntry.TopBearing  = ascender - sdfBitmap.MaxY;
    outEntry.BitmapWidth  = sdfBitmap.MaxX - sdfBitmap.MinX;
    outEntry.BitmapHeight = sdfBitmap.MaxY - sdfBitmap.MinY;

    outEntry.AtlasU0 = static_cast<float>(atlasRect.X) / m_AtlasSize;
    outEntry.AtlasV0 = static_cast<float>(atlasRect.Y) / m_AtlasSize;
    outEntry.AtlasU1 = static_cast<float>(atlasRect.X + bitmapWidth)  / m_AtlasSize;
    outEntry.AtlasV1 = static_cast<float>(atlasRect.Y + bitmapHeight) / m_AtlasSize;

    PendingGlyphUpload upload;
    upload.Pixels = std::move(sdfBitmap.Data);
    upload.Width = bitmapWidth;
    upload.Height = bitmapHeight;
    upload.AtlasX = atlasRect.X;
    upload.AtlasY = atlasRect.Y;
    upload.BytesPerPixel = sdfBitmap.BytesPerPixel;
    m_PendingUploads.push_back(std::move(upload));

    m_CachedGlyphs[codepoint] = outEntry;
    return Gem::Result::Success;
}

} // namespace Canvas
