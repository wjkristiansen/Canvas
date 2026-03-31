#pragma once
//================================================================================================
// GlyphAtlas - Glyph atlas entry, CPU-side packer, and Gem interface wrapper
//
// GlyphAtlasEntry: describes a cached glyph's atlas location and metrics
// CGlyphAtlas: CPU-side packing + GPU texture management
// CGlyphAtlasImpl: Gem interface wrapper (implements XGlyphAtlas)
//================================================================================================

#include "CanvasCore.h"
#include "CanvasGfx.h"
#include "Gem.hpp"
#include "SDFGenerator.h"
#include "RectanglePacker.h"
#include <unordered_map>
#include <memory>

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
// CGlyphAtlas - manages cached glyphs in a texture
//------------------------------------------------------------------------------------------------

class CGlyphAtlas
{
public:
    CGlyphAtlas(uint32_t initialAtlasSize = 512);
    ~CGlyphAtlas();
    
    Gem::Result Initialize(XGfxDevice *pDevice, XGfxRenderQueue *pRenderQueue);
    
    Gem::Result CacheGlyph(uint32_t codepoint, const CTrueTypeFont &font,
                          const CSDFGenerator::Config &sdfConfig,
                          GlyphAtlasEntry &outEntry);
    
    bool GetCachedGlyph(uint32_t codepoint, GlyphAtlasEntry &outEntry) const;
    
    XGfxSurface* GetAtlasTexture() const { return m_pAtlasTexture.Get(); }
    
    size_t GetCachedGlyphCount() const { return m_CachedGlyphs.size(); }

private:
    XGfxDevice *m_pDevice;
    XGfxRenderQueue *m_pRenderQueue;
    
    Gem::TGemPtr<XGfxSurface> m_pAtlasTexture;
    uint32_t m_AtlasSize;
    
    std::unique_ptr<CRectanglePacker> m_pPacker;
    
    std::unordered_map<uint32_t, GlyphAtlasEntry> m_CachedGlyphs;
    
    Gem::Result CreateAtlasTexture(uint32_t size);
};

//------------------------------------------------------------------------------------------------
// CGlyphAtlasImpl - Gem interface implementation of XGlyphAtlas
//------------------------------------------------------------------------------------------------

class CGlyphAtlasImpl : public Gem::TGeneric<XGlyphAtlas>
{
private:
    std::unique_ptr<CGlyphAtlas> m_pAtlas;
    CSDFGenerator::Config m_SDFConfig;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XGlyphAtlas)
    END_GEM_INTERFACE_MAP()

    CGlyphAtlasImpl(uint32_t atlasSize)
    {
        m_pAtlas = std::make_unique<CGlyphAtlas>(atlasSize);

        m_SDFConfig.TextureSize = 64;
        m_SDFConfig.SampleRange = 0.1f;
        m_SDFConfig.GenerateMSDF = false;
    }

    void Initialize() {}

    virtual ~CGlyphAtlasImpl() {}

    Gem::Result InitializeGPU(XGfxDevice *pDevice, XGfxRenderQueue *pRenderQueue)
    {
        if (!pDevice || !pRenderQueue)
            return Gem::Result::BadPointer;
        return m_pAtlas->Initialize(pDevice, pRenderQueue);
    }

    void SetSDFConfig(const CSDFGenerator::Config &config)
    {
        m_SDFConfig = config;
    }

    // XCanvasElement methods
    GEMMETHOD_(XCanvas*, GetCanvas)() override { return nullptr; }
    GEMMETHOD_(PCSTR, GetTypeName)() override { return "XGlyphAtlas"; }
    GEMMETHOD(Register)(XCanvas *pCanvas) override { UNREFERENCED_PARAMETER(pCanvas); return Gem::Result::Success; }
    GEMMETHOD(Unregister)() override { return Gem::Result::Success; }
    
    // XNamedElement methods
    GEMMETHOD_(PCSTR, GetName)() override { return "GlyphAtlas"; }
    GEMMETHOD_(void, SetName)(PCSTR szName) override { UNREFERENCED_PARAMETER(szName); }
    
    // XGlyphAtlas methods
    GEMMETHOD_(XGfxSurface*, GetAtlasTexture)() override
    {
        return m_pAtlas->GetAtlasTexture();
    }
    
    // Internal caching methods
    Gem::Result InternalCacheGlyph(uint32_t codepoint, XFont *pFont, GlyphAtlasEntry &outEntry);

    bool InternalGetCachedGlyph(uint32_t codepoint, GlyphAtlasEntry &outEntry)
    {
        return m_pAtlas->GetCachedGlyph(codepoint, outEntry);
    }
};

} // namespace Canvas
