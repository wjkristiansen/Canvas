#pragma once
//================================================================================================
// CFont - Gem interface implementation wrapping CTrueTypeFont
//================================================================================================

#include "CanvasCore.h"
#include "CanvasGfx.h"
#include "Gem.hpp"
#include "Font.h"
#include <memory>
#include <string>

namespace Canvas
{

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

    // XCanvasElement methods
    GEMMETHOD_(XCanvas*, GetCanvas)() override { return nullptr; }
    GEMMETHOD_(PCSTR, GetTypeName)() override { return "XFont"; }
    GEMMETHOD(Register)(XCanvas *pCanvas) override { UNREFERENCED_PARAMETER(pCanvas); return Gem::Result::Success; }
    GEMMETHOD(Unregister)() override { return Gem::Result::Success; }
    
    // XNamedElement methods
    GEMMETHOD_(PCSTR, GetName)() override { return m_FontName.c_str(); }
    GEMMETHOD_(void, SetName)(PCSTR szName) override { m_FontName = szName ? szName : ""; }

    // Internal methods
    uint16_t GetGlyphIndex(uint32_t codepoint)
    {
        return m_pFontData->GetGlyphIndex(codepoint);
    }

    Gem::Result GetGlyphOutline(uint32_t codepoint, GlyphOutline &outline)
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

    CTrueTypeFont* GetFontData()
    {
        return m_pFontData.get();
    }
};

} // namespace Canvas
