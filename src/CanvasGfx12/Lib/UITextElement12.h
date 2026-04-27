//================================================================================================
// CUITextElement12 - D3D12 implementation of XUITextElement
//
// GPU-aware text element with SDF glyph caching and per-glyph instance generation.
//================================================================================================

#pragma once

#include "CanvasGfx12.h"
#include "GlyphAtlas.h"
#include "Font.h"
#include "FontImpl.h"
#include "TextLayout.h"
#include "../HLSL/HlslTypes.h"
#include <string>
#include <vector>

using GlyphInstance = HlslTypes::HlslGlyphInstance;

class CUITextElement12 : public TGfxElement<Canvas::XUITextElement>
{
    // Content
    std::string m_Text;
    Canvas::XFont* m_pFont = nullptr;
    Canvas::CGlyphCache* m_pGlyphCache = nullptr;
    Canvas::TextLayoutConfig m_Config;
    std::vector<GlyphInstance> m_CachedGlyphs;

    // State
    Canvas::XUIGraphNode* m_pAttachedNode = nullptr;
    Canvas::Math::FloatVector2 m_LocalOffset = {};
    Canvas::GfxResourceAllocation m_GlyphSRV;
    bool m_Visible = true;
    bool m_Dirty = true;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XUIElement)
        GEM_INTERFACE_ENTRY(Canvas::XUITextElement)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CUITextElement12() = default;
    CUITextElement12(Canvas::XCanvas* pCanvas, Canvas::CGlyphCache* pGlyphCache)
        : TGfxElement(pCanvas)
        , m_pGlyphCache(pGlyphCache)
    {}

    void Initialize() {}
    void Uninitialize() {}

    // XUIElement
    GEMMETHOD_(Canvas::UIElementType, GetType)() const override { return Canvas::UIElementType::Text; }
    GEMMETHOD_(bool, IsVisible)() const override { return m_Visible; }
    GEMMETHOD_(void, SetVisible)(bool visible) override { m_Visible = visible; }
    GEMMETHOD_(Canvas::XUIGraphNode*, GetAttachedNode)() override { return m_pAttachedNode; }
    GEMMETHOD(Detach)() override;
    GEMMETHOD(NotifyNodeContextChanged)(_In_ Canvas::XUIGraphNode* pNode) override { (void)pNode; return Gem::Result::Success; }
    GEMMETHOD(Update)() override;
    GEMMETHOD_(bool, HasContent)() const override { return !m_Text.empty() && !m_CachedGlyphs.empty(); }
    GEMMETHOD_(const Canvas::Math::FloatVector2&, GetLocalOffset)() const override { return m_LocalOffset; }
    GEMMETHOD_(void, SetLocalOffset)(const Canvas::Math::FloatVector2& offset) override { m_LocalOffset = offset; }

    // XUITextElement
    GEMMETHOD_(void, SetText)(PCSTR utf8Text) override;
    GEMMETHOD_(PCSTR, GetText)() const override { return m_Text.c_str(); }
    GEMMETHOD_(void, SetFont)(Canvas::XFont* pFont) override;
    GEMMETHOD_(void, SetLayoutConfig)(const Canvas::TextLayoutConfig& config) override;
    GEMMETHOD_(const Canvas::TextLayoutConfig&, GetLayoutConfig)() const override { return m_Config; }

    // Internal
    void SetAttachedNode(Canvas::XUIGraphNode* pNode) { m_pAttachedNode = pNode; }
    uint32_t GetGlyphCount() const { return static_cast<uint32_t>(m_CachedGlyphs.size()); }
    const void* GetGlyphData() const { return m_CachedGlyphs.data(); }
    const Canvas::GfxResourceAllocation& GetGlyphBuffer() const { return m_GlyphSRV; }
    void SetGlyphBuffer(const Canvas::GfxResourceAllocation& buffer) { m_GlyphSRV = buffer; }

private:
    Gem::Result RegenerateGlyphs();
};
