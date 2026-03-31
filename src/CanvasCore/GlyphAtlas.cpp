//================================================================================================
// GlyphAtlas - Implementation
//================================================================================================

#include "pch.h"
#include "GlyphAtlas.h"
#include "FontImpl.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// CGlyphAtlas Implementation
//------------------------------------------------------------------------------------------------

CGlyphAtlas::CGlyphAtlas(uint32_t initialAtlasSize)
    : m_pDevice(nullptr)
    , m_pRenderQueue(nullptr)
    , m_AtlasSize(initialAtlasSize)
{
    m_pPacker = std::make_unique<CRectanglePacker>(initialAtlasSize, initialAtlasSize);
}

CGlyphAtlas::~CGlyphAtlas()
{
}

Gem::Result CGlyphAtlas::Initialize(XGfxDevice *pDevice, XGfxRenderQueue *pRenderQueue)
{
    if (!pDevice || !pRenderQueue)
        return Gem::Result::BadPointer;
    
    m_pDevice = pDevice;
    m_pRenderQueue = pRenderQueue;
    
    return CreateAtlasTexture(m_AtlasSize);
}

Gem::Result CGlyphAtlas::CreateAtlasTexture(uint32_t size)
{
    if (!m_pDevice)
        return Gem::Result::NotFound;
    
    m_AtlasSize = size;
    m_pPacker = std::make_unique<CRectanglePacker>(size, size);
    
    GfxSurfaceDesc desc = GfxSurfaceDesc::SurfaceDesc2D(
        GfxFormat::R8_UNorm,
        size, size,
        Canvas::SurfaceFlag_ShaderResource,
        1
    );
    
    return m_pDevice->CreateSurface(desc, (XGfxSurface**)&m_pAtlasTexture);
}

bool CGlyphAtlas::GetCachedGlyph(uint32_t codepoint, GlyphAtlasEntry &outEntry) const
{
    auto it = m_CachedGlyphs.find(codepoint);
    if (it != m_CachedGlyphs.end())
    {
        outEntry = it->second;
        return true;
    }
    return false;
}

Gem::Result CGlyphAtlas::CacheGlyph(uint32_t codepoint, const CTrueTypeFont &font,
                                    const CSDFGenerator::Config &sdfConfig,
                                    GlyphAtlasEntry &outEntry)
{
    // Check if already cached
    if (GetCachedGlyph(codepoint, outEntry))
        return Gem::Result::Success;
    
    uint16_t glyphIndex = font.GetGlyphIndex(codepoint);
    
    GlyphOutline outline;
    if (!font.GetGlyphOutline(glyphIndex, outline))
        return Gem::Result::NotFound;
    
    SDFBitmap sdfBitmap;
    CSDFGenerator generator;
    if (!generator.Generate(outline, font, sdfConfig, sdfBitmap))
    {
        // Whitespace or outline-less glyph — metrics-only entry
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
        return Gem::Result::Success;
    }
    
    uint32_t bitmapWidth = sdfConfig.TextureSize;
    uint32_t bitmapHeight = sdfConfig.TextureSize;
    
    CRectanglePacker::Rect atlasRect;
    if (!m_pPacker->Allocate(bitmapWidth, bitmapHeight, atlasRect))
    {
        return Gem::Result::OutOfMemory;
    }
    
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
    
    Gem::Result uploadResult = m_pRenderQueue->UploadTextureRegion(
        m_pAtlasTexture.Get(),
        atlasRect.X, atlasRect.Y,
        bitmapWidth, bitmapHeight,
        sdfBitmap.Data.data(),
        bitmapWidth * sdfBitmap.BytesPerPixel,
        Canvas::GfxRenderContext::UI);
    if (Gem::Failed(uploadResult))
        return uploadResult;

    m_CachedGlyphs[codepoint] = outEntry;
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
// CGlyphAtlasImpl - InternalCacheGlyph
//------------------------------------------------------------------------------------------------

Gem::Result CGlyphAtlasImpl::InternalCacheGlyph(uint32_t codepoint, XFont *pFont, GlyphAtlasEntry &outEntry)
{
    if (!pFont)
        return Gem::Result::BadPointer;

    CFont *pFontImpl = static_cast<CFont*>(pFont);
    CTrueTypeFont *pFontData = pFontImpl->GetFontData();
    if (!pFontData)
        return Gem::Result::BadPointer;
    return m_pAtlas->CacheGlyph(codepoint, *pFontData, m_SDFConfig, outEntry);
}

} // namespace Canvas
