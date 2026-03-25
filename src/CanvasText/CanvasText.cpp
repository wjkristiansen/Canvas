//================================================================================================
// CanvasText - Implementation
//
// Gem interface implementations for fonts and glyph atlases.
//================================================================================================

#include "CanvasText.h"
#include "Gem.hpp"
#include <memory>
#include <string>

// Internal implementation headers
#include "Font.h"
#include "SDFGenerator.h"
#include "GlyphAtlas.h"
#include "TextLayout.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Note: We use Gem::TGenericImpl as a private base that provides QueryInterface implementation.
// But we don't inherit from it directly - instead we implement the Gem interface methods manually.
// This avoids issues with constructor forwarding to abstract base classes.

class CFont : public Gem::TGeneric<XFont>
{
private:
    std::unique_ptr<CTrueTypeFont> m_pFontData;
    std::string m_FontName;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XFont)
    END_GEM_INTERFACE_MAP()

    CFont(const char *pFontName)
        : m_FontName(pFontName)
    {
    }

    void Initialize()
    {
        m_pFontData = std::make_unique<CTrueTypeFont>();
    }

    virtual ~CFont() {}

    // Load from buffer
    Gem::Result LoadFromBuffer(const uint8_t *pData, size_t dataSize)
    {
        if (!m_pFontData->LoadFromBuffer(pData, dataSize))
            return Gem::Result::NotFound;
        return Gem::Result::Success;
    }

    // XCanvasElement methods (with stub implementations)
    GEMMETHOD_(XCanvas*, GetCanvas)() override { return nullptr; }
    GEMMETHOD_(PCSTR, GetTypeName)() override { return "XFont"; }
    GEMMETHOD(Register)(XCanvas *pCanvas) override { UNREFERENCED_PARAMETER(pCanvas); return Gem::Result::Success; }
    GEMMETHOD(Unregister)() override { return Gem::Result::Success; }
    
    // XNamedElement methods
    GEMMETHOD_(PCSTR, GetName)() override { return m_FontName.c_str(); }
    GEMMETHOD_(void, SetName)(PCSTR szName) override { m_FontName = szName ? szName : ""; }

    // XFont methods
    GEMMETHOD_(uint16_t, GetGlyphIndex)(uint32_t codepoint) override
    {
        return m_pFontData->GetGlyphIndex(codepoint);
    }

    GEMMETHOD(GetGlyphOutline)(uint32_t codepoint, GlyphOutline &outline) override
    {
        uint16_t glyphIndex = m_pFontData->GetGlyphIndex(codepoint);
        if (!m_pFontData->GetGlyphOutline(glyphIndex, outline))
            return Gem::Result::NotFound;
        return Gem::Result::Success;
    }

    GEMMETHOD_(float, GetAscender)() override
    {
        return m_pFontData->NormalizeY(m_pFontData->GetMetrics().Ascender);
    }

    GEMMETHOD_(float, GetDescender)() override
    {
        return m_pFontData->NormalizeY(m_pFontData->GetMetrics().Descender);
    }

    GEMMETHOD_(float, GetLineGap)() override
    {
        return m_pFontData->NormalizeY(m_pFontData->GetMetrics().LineGap);
    }

    GEMMETHOD_(uint16_t, GetUnitsPerEm)() override
    {
        return m_pFontData->GetUnitsPerEm();
    }

    GEMMETHOD_(CTrueTypeFont*, GetFontData)() override
    {
        return m_pFontData.get();
    }

};

//------------------------------------------------------------------------------------------------
// CGlyphAtlasImpl - Implementation of XGlyphAtlas interface  
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

        // Default SDF config
        m_SDFConfig.TextureSize = 64;
        m_SDFConfig.SampleRange = 0.1f;  // 10% of cap-height: enough gradient for AA at 32px
        m_SDFConfig.GenerateMSDF = false;
    }

    void Initialize() {}

    virtual ~CGlyphAtlasImpl() {}

    // Initialize with GPU resources
    Gem::Result InitializeGPU(XGfxDevice *pDevice, XGfxRenderQueue *pRenderQueue)
    {
        if (!pDevice || !pRenderQueue)
            return Gem::Result::BadPointer;
        return m_pAtlas->Initialize(pDevice, pRenderQueue);
    }

    // Set SDF generation config
    void SetSDFConfig(const CSDFGenerator::Config &config)
    {
        m_SDFConfig = config;
    }

    // XCanvasElement methods (with stub implementations)
    GEMMETHOD_(XCanvas*, GetCanvas)() override { return nullptr; }
    GEMMETHOD_(PCSTR, GetTypeName)() override { return "XGlyphAtlas"; }
    GEMMETHOD(Register)(XCanvas *pCanvas) override { UNREFERENCED_PARAMETER(pCanvas); return Gem::Result::Success; }
    GEMMETHOD(Unregister)() override { return Gem::Result::Success; }
    
    // XNamedElement methods
    GEMMETHOD_(PCSTR, GetName)() override { return "GlyphAtlas"; }
    GEMMETHOD_(void, SetName)(PCSTR szName) override { UNREFERENCED_PARAMETER(szName); } // Ignore name for now
    
    // XGlyphAtlas methods (only GetAtlasTexture is part of public interface)
    GEMMETHOD_(XGfxSurface*, GetAtlasTexture)() override
    {
        return m_pAtlas->GetAtlasTexture();
    }
    
    // Internal caching methods (NOT part of XGlyphAtlas interface)
    // Called by QueueTextRender() internally - applications don't use these directly
    Gem::Result InternalCacheGlyph(uint32_t codepoint, XFont *pFont, GlyphAtlasEntry &outEntry)
    {
        if (!pFont)
            return Gem::Result::BadPointer;

        CFont *pFontImpl = static_cast<CFont*>(pFont);
        CTrueTypeFont *pFontData = pFontImpl->GetFontData();
        if (!pFontData)
            return Gem::Result::BadPointer;
        return m_pAtlas->CacheGlyph(codepoint, *pFontData, m_SDFConfig, outEntry);
    }

    bool InternalGetCachedGlyph(uint32_t codepoint, GlyphAtlasEntry &outEntry)
    {
        return m_pAtlas->GetCachedGlyph(codepoint, outEntry);
    }
};

//------------------------------------------------------------------------------------------------
// Factory Functions
//------------------------------------------------------------------------------------------------

Gem::Result CreateFontFromBuffer(
    XCanvas *pCanvas,
    const uint8_t *pTTFData,
    size_t dataSize,
    PCSTR pFontName,
    XFont **ppFont)
{
    if (!pCanvas || !pTTFData || !ppFont)
        return Gem::Result::BadPointer;

    Gem::TGemPtr<CFont> pFont;
    Gem::Result result = Gem::TGenericImpl<CFont>::Create(&pFont, pFontName);
    if (Gem::Failed(result))
        return result;

    result = pFont->LoadFromBuffer(pTTFData, dataSize);
    if (Gem::Failed(result))
        return result;

    *ppFont = pFont.Detach();
    return Gem::Result::Success;
}

Gem::Result CreateGlyphAtlas(
    XCanvas *pCanvas,
    XGfxDevice *pGfxDevice,
    XGfxRenderQueue *pRenderQueue,
    uint32_t initialAtlasSize,
    XGlyphAtlas **ppAtlas)
{
    if (!pCanvas || !pGfxDevice || !pRenderQueue || !ppAtlas)
        return Gem::Result::BadPointer;

    Gem::TGemPtr<CGlyphAtlasImpl> pAtlas;
    Gem::Result result = Gem::TGenericImpl<CGlyphAtlasImpl>::Create(&pAtlas, initialAtlasSize);
    if (Gem::Failed(result))
        return result;

    result = pAtlas->InitializeGPU(pGfxDevice, pRenderQueue);
    if (Gem::Failed(result))
        return result;

    *ppAtlas = pAtlas.Detach();
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
// Queue text rendering
//
// High-level helper that combines layout + GPU buffer creation + render submission.
// Generates geometry for UTF-8 text string and queues to render queue as TextChunkData.
//------------------------------------------------------------------------------------------------

Gem::Result QueueTextRender(
    XGfxRenderQueue *pRenderQueue,
    XGfxDevice *pGfxDevice,
    PCSTR utf8Text,
    XFont *pFont,
    XGlyphAtlas *pAtlas,
    const Math::FloatVector3& screenPosition,
    const TextLayoutConfig& config,
    XLogger *pLogger)
{
    if (!pRenderQueue || !pGfxDevice || !utf8Text || !pFont || !pAtlas)
    {
        LogError(pLogger, "QueueTextRender: null argument (queue=%p device=%p text=%p font=%p atlas=%p)",
            pRenderQueue, pGfxDevice, utf8Text, pFont, pAtlas);
        return Gem::Result::BadPointer;
    }

    if (!utf8Text[0])  // Empty string
        return Gem::Result::Success;

    // Downcast to implementation class for internal cache access
    CGlyphAtlasImpl* pAtlasImpl = static_cast<CGlyphAtlasImpl*>(pAtlas);

    try
    {
        // Get font data
        CTrueTypeFont* pFontData = pFont->GetFontData();
        if (!pFontData)
        {
            LogError(pLogger, "QueueTextRender: font has no internal data");
            return Gem::Result::BadPointer;
        }

        // Decode UTF-8 to codepoints
        std::vector<uint32_t> codepoints;
        Gem::Result result = CTextLayout::DecodeUtf8(utf8Text, codepoints);
        if (Gem::Failed(result))
        {
            LogError(pLogger, "QueueTextRender: UTF-8 decode failed (result=0x%08X)", (unsigned)result);
            return result;
        }

        // Cache and collect glyph entries
        std::vector<GlyphAtlasEntry> glyphEntries;
        glyphEntries.reserve(codepoints.size());
        Math::FloatVector3 cursorPos = screenPosition;
        const CTrueTypeFont::FontMetrics& metrics = pFontData->GetMetrics();
        float lineHeightUnits = (metrics.Ascender - metrics.Descender + metrics.LineGap);
        float lineHeightPixels = lineHeightUnits * (config.FontSize / pFontData->GetUnitsPerEm());

        for (uint32_t codepoint : codepoints)
        {
            if (codepoint == '\n')
            {
                cursorPos.X = screenPosition.X;
                cursorPos.Y += lineHeightPixels * config.LineHeight;
                continue;
            }

            if (codepoint == '\t')
            {
                codepoint = ' ';
                for (int i = 0; i < 4; i++)
                {
                    GlyphAtlasEntry entry = {};
                    result = pAtlasImpl->InternalCacheGlyph(codepoint, pFont, entry);
                    if (Gem::Failed(result))
                    {
                        LogError(pLogger, "QueueTextRender: CacheGlyph failed for tab-space (result=0x%08X)", (unsigned)result);
                        return result;
                    }
                    cursorPos.X += entry.AdvanceWidth * config.FontSize;
                }
                continue;
            }

            if (codepoint < 32)  // Skip remaining control characters
                continue;

            GlyphAtlasEntry entry = {};
            result = pAtlasImpl->InternalCacheGlyph(codepoint, pFont, entry);
            if (Gem::Failed(result))
            {
                LogError(pLogger, "QueueTextRender: CacheGlyph failed for codepoint U+%04X (result=0x%08X)", codepoint, (unsigned)result);
                return result;
            }

            glyphEntries.push_back(entry);
        }

        // Generate vertex data (6 vertices per glyph quad)
        std::vector<TextVertex> vertices;
        vertices.reserve(glyphEntries.size() * 6);
        cursorPos = screenPosition;

        size_t glyphIdx = 0;
        for (uint32_t codepoint : codepoints)
        {
            if (codepoint == '\n')
            {
                cursorPos.X = screenPosition.X;
                cursorPos.Y += lineHeightPixels * config.LineHeight;
                continue;
            }

            if (codepoint < 32)
            {
                if (codepoint == '\t')
                    glyphIdx += 4;
                continue;
            }

            if (glyphIdx >= glyphEntries.size())
                break;

            float advance = CTextLayout::LayoutGlyph(
                codepoint,
                *pFontData,
                glyphEntries[glyphIdx],
                cursorPos,
                config.Color,
                config.FontSize,
                vertices);

            cursorPos.X += advance * config.FontSize;
            glyphIdx++;
        }

        if (vertices.empty())
        {
            LogWarn(pLogger, "QueueTextRender: no renderable glyphs produced for text \"%s\"", utf8Text);
            return Gem::Result::Success;
        }

        XGfxSurface* pAtlasTexture = pAtlas->GetAtlasTexture();
        if (!pAtlasTexture)
        {
            LogError(pLogger, "QueueTextRender: atlas texture is null");
            return Gem::Result::InvalidArg;
        }

        result = pRenderQueue->DrawText(vertices.data(), static_cast<uint32_t>(vertices.size()),
            pAtlasTexture, Math::FloatVector4(screenPosition.X, screenPosition.Y, screenPosition.Z, 0.0f));
        if (Gem::Failed(result))
            LogError(pLogger, "QueueTextRender: DrawText failed (result=0x%08X)", (unsigned)result);

        return result;
    }
    catch (std::bad_alloc)
    {
        LogError(pLogger, "QueueTextRender: out of memory");
        return Gem::Result::OutOfMemory;
    }
    catch (Gem::GemError &e)
    {
        LogError(pLogger, "QueueTextRender: exception (result=0x%08X)", (unsigned)e.Result());
        return e.Result();
    }
}

} // namespace Canvas
