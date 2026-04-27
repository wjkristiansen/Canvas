//================================================================================================
// UIElement - UI graph nodes and element types
//
// CUIGraphNodeImpl: XUIGraphNode implementation — tree structure and screen-space position
// CUITextElement: text element with per-glyph instance generation
// CUIRectElement: rectangle element (vertex-buffer-free; geometry derived on GPU)
//================================================================================================

#pragma once
#include "Canvas.h"
#include "CanvasElement.h"
#include "TextLayout.h"
#include "GlyphAtlas.h"
#include "../HLSL/HlslTypes.h"

// FloatVector4 has alignas(16) for SIMD. Classes containing it as a member
// (e.g. CUIRectElement::m_FillColor) trigger C4324. Suppress until a
// dedicated Color4f type without forced alignment replaces FloatVector4 for colors.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // structure padded due to alignment specifier
#endif

namespace Canvas
{

// Compact per-glyph instance data for GPU-driven text rendering.
// One instance per visible glyph; the vertex shader expands each to a quad
// using SV_VertexID.  Defined in HlslTypes.h (shared C++/HLSL).
using GlyphInstance = HlslTypes::HlslGlyphInstance;

//================================================================================================
// CUIGraphNodeImpl - XUIGraphNode implementation (repurposed from CUIElementCore)
//================================================================================================

class CUIGraphNodeImpl : public TCanvasElement<XUIGraphNode>
{
public:
    struct ChildNode
    {
        Gem::TGemPtr<XUIGraphNode> pNode;
        ChildNode* pPrev = nullptr;
        ChildNode* pNext = nullptr;
    };

protected:
    CUIGraphNodeImpl* m_pParent = nullptr;
    ChildNode* m_pFirstChild = nullptr;
    ChildNode* m_pLastChild = nullptr;  // Tail pointer for O(1) AddChild
    ChildNode* m_pMyEntry = nullptr;    // This node's entry in parent's child list

    Math::FloatVector2 m_LocalPosition = {};

    std::vector<Gem::TGemPtr<XUIElement>> m_Elements;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XUIGraphNode)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XNamedElement)
    END_GEM_INTERFACE_MAP()

    CUIGraphNodeImpl() = default;
    CUIGraphNodeImpl(XCanvas* pCanvas) : TCanvasElement(pCanvas) {}
    ~CUIGraphNodeImpl();

    void Initialize() {}
    void Uninitialize() {}

    // XUIGraphNode interface
    GEMMETHOD(AddChild)(_In_ XUIGraphNode* pChild) final;
    GEMMETHOD(RemoveChild)(_In_ XUIGraphNode* pChild) final;
    GEMMETHOD_(XUIGraphNode*, GetParent)() final { return m_pParent; }
    GEMMETHOD_(XUIGraphNode*, GetFirstChild)() final;
    GEMMETHOD_(XUIGraphNode*, GetNextSibling)() final;

    GEMMETHOD_(const Math::FloatVector2&, GetLocalPosition)() const final { return m_LocalPosition; }
    GEMMETHOD_(void, SetLocalPosition)(const Math::FloatVector2& position) final;
    GEMMETHOD_(Math::FloatVector2, GetGlobalPosition)() final;

    GEMMETHOD(BindElement)(_In_ XUIElement* pElement) final;
    GEMMETHOD_(UINT, GetBoundElementCount)() final { return static_cast<UINT>(m_Elements.size()); }
    GEMMETHOD_(XUIElement*, GetBoundElement)(UINT index) final { return m_Elements[index].Get(); }

    // Internal
    void UnbindElement(XUIElement* pElement);

private:
    static CUIGraphNodeImpl* GetImpl(XUIGraphNode* pNode);
    ChildNode* FindChildNode(XUIGraphNode* pChild);
};

//================================================================================================
// CUITextElement - Text element with per-glyph instance generation
//================================================================================================

class CUITextElement : public TCanvasElement<XUITextElement>
{
    // Content
    std::string m_Text;
    XFont* m_pFont = nullptr;
    CGlyphCache* m_pGlyphCache = nullptr;
    TextLayoutConfig m_Config;
    std::vector<GlyphInstance> m_CachedGlyphs;

    // State
    XUIGraphNode* m_pAttachedNode = nullptr;
    GfxResourceAllocation m_GlyphBuffer;
    bool m_Visible = true;
    bool m_Dirty = true;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XUIElement)
        GEM_INTERFACE_ENTRY(XUITextElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XNamedElement)
    END_GEM_INTERFACE_MAP()

    CUITextElement() = default;
    CUITextElement(XCanvas* pCanvas, CGlyphCache* pGlyphCache, XGfxSurface* /*pAtlasSurface*/)
        : TCanvasElement(pCanvas)
        , m_pGlyphCache(pGlyphCache)
    {}
    void Initialize() {}
    void Uninitialize() {}

    void SetGlyphCache(CGlyphCache* pCache) { m_pGlyphCache = pCache; }

    // XUIElement
    GEMMETHOD_(UIElementType, GetType)() const override { return UIElementType::Text; }
    GEMMETHOD_(bool, IsVisible)() const override { return m_Visible; }
    GEMMETHOD_(void, SetVisible)(bool visible) override { m_Visible = visible; }
    GEMMETHOD_(XUIGraphNode*, GetAttachedNode)() override { return m_pAttachedNode; }
    GEMMETHOD_(const GfxResourceAllocation&, GetVertexBuffer)() const override { return m_GlyphBuffer; }
    GEMMETHOD_(void, SetVertexBuffer)(const GfxResourceAllocation& buffer) override { m_GlyphBuffer = buffer; }

    // XUITextElement
    GEMMETHOD_(void, SetText)(PCSTR utf8Text) override;
    GEMMETHOD_(PCSTR, GetText)() const override { return m_Text.c_str(); }
    GEMMETHOD_(void, SetFont)(XFont* pFont) override;
    GEMMETHOD_(void, SetLayoutConfig)(const TextLayoutConfig& config) override;
    GEMMETHOD_(const TextLayoutConfig&, GetLayoutConfig)() const override { return m_Config; }

    // Internal
    void SetAttachedNode(XUIGraphNode* pNode) { m_pAttachedNode = pNode; }
    bool IsDirty() const { return m_Dirty; }
    void ClearDirty() { m_Dirty = false; }
    Gem::Result RegenerateGlyphs();
    uint32_t GetGlyphCount() const { return static_cast<uint32_t>(m_CachedGlyphs.size()); }
    const void* GetGlyphData() const { return m_CachedGlyphs.data(); }
    bool HasContent() const { return !m_Text.empty(); }
};

//================================================================================================
// CUIRectElement - Rectangle element (vertex-buffer-free; geometry derived on GPU)
//================================================================================================

class CUIRectElement : public TCanvasElement<XUIRectElement>
{
    Math::FloatVector4 m_FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Math::FloatVector2 m_Size = {};

    XUIGraphNode* m_pAttachedNode = nullptr;
    GfxResourceAllocation m_VertexBuffer;   // Stub — required by XUIElement interface
    bool m_Visible = true;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XUIElement)
        GEM_INTERFACE_ENTRY(XUIRectElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XNamedElement)
    END_GEM_INTERFACE_MAP()

    CUIRectElement() = default;
    CUIRectElement(XCanvas* pCanvas) : TCanvasElement(pCanvas) {}
    void Initialize() {}
    void Uninitialize() {}

    // XUIElement
    GEMMETHOD_(UIElementType, GetType)() const override { return UIElementType::Rect; }
    GEMMETHOD_(bool, IsVisible)() const override { return m_Visible; }
    GEMMETHOD_(void, SetVisible)(bool visible) override { m_Visible = visible; }
    GEMMETHOD_(XUIGraphNode*, GetAttachedNode)() override { return m_pAttachedNode; }
    GEMMETHOD_(const GfxResourceAllocation&, GetVertexBuffer)() const override { return m_VertexBuffer; }
    GEMMETHOD_(void, SetVertexBuffer)(const GfxResourceAllocation&) override {}

    // XUIRectElement
    GEMMETHOD_(void, SetSize)(const Math::FloatVector2& size) override;
    GEMMETHOD_(const Math::FloatVector2&, GetSize)() const override { return m_Size; }
    GEMMETHOD_(void, SetFillColor)(const Math::FloatVector4& color) override;
    GEMMETHOD_(const Math::FloatVector4&, GetFillColor)() const override { return m_FillColor; }

    // Internal
    void SetAttachedNode(XUIGraphNode* pNode) { m_pAttachedNode = pNode; }
    bool HasContent() const { return m_Size.X > 0.0f && m_Size.Y > 0.0f; }
};

// Concrete-type cast helpers (used by CUIGraph during tree walk)
inline CUITextElement* AsText(XUIElement* p) { return static_cast<CUITextElement*>(static_cast<XUITextElement*>(p)); }
inline CUIRectElement* AsRect(XUIElement* p) { return static_cast<CUIRectElement*>(static_cast<XUIRectElement*>(p)); }

} // namespace Canvas

#ifdef _MSC_VER
#pragma warning(pop)
#endif
