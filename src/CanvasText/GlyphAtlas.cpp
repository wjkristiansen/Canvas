//================================================================================================
// CanvasText - Glyph Atlas Manager Implementation
//================================================================================================

#include "GlyphAtlas.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Rectangle Packer Implementation
//------------------------------------------------------------------------------------------------

CRectanglePacker::CRectanglePacker(uint32_t textureWidth, uint32_t textureHeight)
    : m_TextureWidth(textureWidth), m_TextureHeight(textureHeight)
{
}

bool CRectanglePacker::CanAllocate(const Rect &candidate) const
{
    // Check bounds
    if (candidate.X + candidate.Width > m_TextureWidth ||
        candidate.Y + candidate.Height > m_TextureHeight)
        return false;
    
    // Check overlap with allocated rectangles
    for (const auto &rect : m_AllocatedRects)
    {
        // Check for overlap: rectangles DON'T overlap if:
        // candidate.X+W <= rect.X  OR  candidate.X >= rect.X+W  OR
        // candidate.Y+H <= rect.Y  OR  candidate.Y >= rect.Y+H
        
        if (!(candidate.X + candidate.Width <= rect.X ||
              candidate.X >= rect.X + rect.Width ||
              candidate.Y + candidate.Height <= rect.Y ||
              candidate.Y >= rect.Y + rect.Height))
        {
            return false; // Overlaps
        }
    }
    
    return true;
}

bool CRectanglePacker::Allocate(uint32_t width, uint32_t height, Rect &outRect)
{
    // Simple shelf-based packing: place rects left-to-right, then create new shelf
    
    // First, try to fit in existing rows (shelves)
    uint32_t tryY = 0;
    
    while (tryY + height <= m_TextureHeight)
    {
        // Find rightmost occupied x at this y level
        uint32_t rightmostX = 0;
        
        for (const auto &rect : m_AllocatedRects)
        {
            if (rect.Y <= tryY && tryY < rect.Y + rect.Height)
            {
                rightmostX = std::max(rightmostX, rect.X + rect.Width);
            }
        }
        
        // Try to place at this shelf
        if (rightmostX + width <= m_TextureWidth)
        {
            Rect candidate(rightmostX, tryY, width, height);
            if (CanAllocate(candidate))
            {
                outRect = candidate;
                m_AllocatedRects.push_back(candidate);
                return true;
            }
        }
        
        // Find next shelf: minimum bottom-edge of rects that cover the current shelf row
        uint32_t nextY = m_TextureHeight;
        for (const auto &rect : m_AllocatedRects)
        {
            uint32_t bottom = rect.Y + rect.Height;
            if (rect.Y <= tryY && bottom > tryY)
                nextY = std::min(nextY, bottom);
        }
        
        if (nextY >= m_TextureHeight)
            break; // No more rows
        
        tryY = nextY;
    }
    
    return false;
}

uint32_t CRectanglePacker::GetFreeArea() const
{
    uint32_t totalAllocated = 0;
    for (const auto &rect : m_AllocatedRects)
        totalAllocated += rect.Width * rect.Height;
    
    return GetTotalArea() - totalAllocated;
}

//------------------------------------------------------------------------------------------------
// Glyph Atlas Implementation
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
    
    // Create initial atlas texture
    return CreateAtlasTexture(m_AtlasSize);
}

Gem::Result CGlyphAtlas::CreateAtlasTexture(uint32_t size)
{
    if (!m_pDevice)
        return Gem::Result::NotFound;
    
    m_AtlasSize = size;
    m_pPacker = std::make_unique<CRectanglePacker>(size, size);
    
    // Create texture via XGfxDevice
    // SurfaceFlag_ShaderResource: readable by shaders (on default/GPU heap)
    // Upload is performed via staging buffer + CopyTextureRegion
    GfxSurfaceDesc desc = GfxSurfaceDesc::SurfaceDesc2D(
        GfxFormat::R8_UNorm,
        size, size,
        Canvas::SurfaceFlag_ShaderResource,
        1 // Single mip level
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
    
    // Get glyph index for codepoint
    uint16_t glyphIndex = font.GetGlyphIndex(codepoint);
    
    // Extract glyph outline
    GlyphOutline outline;
    if (!font.GetGlyphOutline(glyphIndex, outline))
        return Gem::Result::NotFound; // Invalid glyph
    
    // Generate SDF bitmap
    SDFBitmap sdfBitmap;
    CSDFGenerator generator;
    if (!generator.Generate(outline, font, sdfConfig, sdfBitmap))
    {
        // Whitespace or otherwise outline-less glyph (e.g. space).
        // Return a metrics-only entry so the cursor advances correctly.
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
    
    // Compute actual bitmap bounds (can be smaller than config.TextureSize)
    // For now, use full config.TextureSize
    uint32_t bitmapWidth = sdfConfig.TextureSize;
    uint32_t bitmapHeight = sdfConfig.TextureSize;
    
    // Allocate space in atlas
    CRectanglePacker::Rect atlasRect;
    if (!m_pPacker->Allocate(bitmapWidth, bitmapHeight, atlasRect))
    {
        // Atlas is full - would need to grow or fail
        // For now, return failure (TODO: dynamic atlas growth)
        return Gem::Result::OutOfMemory;
    }
    
    // Fill entry
    outEntry.Codepoint = codepoint;
    outEntry.GlyphIndex = glyphIndex;
    outEntry.AdvanceWidth = sdfBitmap.AdvanceWidth;
    // LeftBearing uses the padded MinX so the quad's left edge covers the full
    // gradient margin to the left of the ink.
    outEntry.LeftBearing = sdfBitmap.MinX;
    // TopBearing = gap from the font's ascender line down to the padded tile top
    // (may be slightly negative for tall glyphs with upward padding, which is fine).
    float ascender = font.NormalizeY(font.GetMetrics().Ascender);
    outEntry.TopBearing  = ascender - sdfBitmap.MaxY;
    outEntry.BitmapWidth  = sdfBitmap.MaxX - sdfBitmap.MinX;  // Padded glyph width in em
    outEntry.BitmapHeight = sdfBitmap.MaxY - sdfBitmap.MinY;  // Padded glyph height in em
    
    // Compute texture coordinates in [0,1] normalized range.
    // UVs span the full tile with no inset: pixel p within the N-pixel-wide quad
    // gets interpolated UV = atlasX/S + (p+0.5)/N * N/S = (atlasX + p + 0.5)/S,
    // which is exactly the center of atlas texel p.  No inset needed because the
    // padded SDF has already decayed to transparent before the tile boundary.
    outEntry.AtlasU0 = static_cast<float>(atlasRect.X) / m_AtlasSize;
    outEntry.AtlasV0 = static_cast<float>(atlasRect.Y) / m_AtlasSize;
    outEntry.AtlasU1 = static_cast<float>(atlasRect.X + bitmapWidth)  / m_AtlasSize;
    outEntry.AtlasV1 = static_cast<float>(atlasRect.Y + bitmapHeight) / m_AtlasSize;
    
    // Upload SDF bitmap data to the atlas texture region
    Gem::Result uploadResult = m_pRenderQueue->UploadTextureRegion(
        m_pAtlasTexture.Get(),
        atlasRect.X, atlasRect.Y,
        bitmapWidth, bitmapHeight,
        sdfBitmap.Data.data(),
        bitmapWidth * sdfBitmap.BytesPerPixel);
    if (Gem::Failed(uploadResult))
        return uploadResult;

    // Cache the entry
    m_CachedGlyphs[codepoint] = outEntry;
    
    return Gem::Result::Success;
}

} // namespace Canvas
