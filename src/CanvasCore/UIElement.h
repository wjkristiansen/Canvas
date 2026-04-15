//================================================================================================
// UIElement - UI graph nodes and element types
//
// CUIGraphNodeImpl: XGfxUIGraphNode implementation — tree structure and screen-space position
// CUITextElement: text element with cached vertex generation
// CUIRectElement: rectangle element with cached vertex generation
//================================================================================================

#pragma once
#include "Canvas.h"
#include "CanvasElement.h"
#include "TextLayout.h"
#include "GlyphAtlas.h"

// FloatVector4 has alignas(16) for SIMD. Classes containing it as a member
// (e.g. CUIRectElement::m_FillColor) trigger C4324. Suppress until a
// dedicated Color4f type without forced alignment replaces FloatVector4 for colors.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

namespace Canvas
{

//================================================================================================
// CUIGraphNodeImpl - XGfxUIGraphNode implementation (repurposed from CUIElementCore)
//================================================================================================

class CUIGraphNodeImpl : public TCanvasElement<XGfxUIGraphNode>
{
public:
    struct ChildNode
    {
        Gem::TGemPtr<XGfxUIGraphNode> pNode;
        ChildNode* pPrev = nullptr;
        ChildNode* pNext = nullptr;
    };

protected:
    CUIGraphNodeImpl* m_pParent = nullptr;
    ChildNode* m_pFirstChild = nullptr;
    ChildNode* m_pMyEntry = nullptr;    // This node's entry in parent's child list

    Math::FloatVector2 m_LocalPosition = {};

    std::vector<Gem::TGemPtr<XGfxUIElement>> m_Elements;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxUIGraphNode)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XNamedElement)
    END_GEM_INTERFACE_MAP()

    CUIGraphNodeImpl() = default;
    CUIGraphNodeImpl(XCanvas* pCanvas) : TCanvasElement(pCanvas) {}
    ~CUIGraphNodeImpl();

    void Initialize() {}
    void Uninitialize() {}

    // XGfxUIGraphNode interface
    GEMMETHOD(AddChild)(_In_ XGfxUIGraphNode* pChild) final;
    GEMMETHOD(RemoveChild)(_In_ XGfxUIGraphNode* pChild) final;
    GEMMETHOD_(XGfxUIGraphNode*, GetParent)() final { return m_pParent; }
    GEMMETHOD_(XGfxUIGraphNode*, GetFirstChild)() final;
    GEMMETHOD_(XGfxUIGraphNode*, GetNextSibling)() final;

    GEMMETHOD_(const Math::FloatVector2&, GetLocalPosition)() const final { return m_LocalPosition; }
    GEMMETHOD_(void, SetLocalPosition)(const Math::FloatVector2& position) final;
    GEMMETHOD_(Math::FloatVector2, GetGlobalPosition)() final;

    GEMMETHOD(BindElement)(_In_ XGfxUIElement* pElement) final;
    GEMMETHOD_(UINT, GetBoundElementCount)() final { return static_cast<UINT>(m_Elements.size()); }
    GEMMETHOD_(XGfxUIElement*, GetBoundElement)(UINT index) final { return m_Elements[index].Get(); }

    // Internal
    void UnbindElement(XGfxUIElement* pElement);

private:
    static CUIGraphNodeImpl* GetImpl(XGfxUIGraphNode* pNode);
    ChildNode* FindChildNode(XGfxUIGraphNode* pChild);
};

//================================================================================================
// CUITextElement - Text element with cached vertex generation
//================================================================================================

class CUITextElement : public TCanvasElement<XGfxUITextElement>
{
    // Content
    std::string m_Text;
    XFont* m_pFont = nullptr;
    CGlyphAtlasImpl* m_pAtlas = nullptr;
    TextLayoutConfig m_Config;
    std::vector<TextVertex> m_CachedVertices;

    // State
    XGfxUIGraphNode* m_pAttachedNode = nullptr;
    GfxResourceAllocation m_VertexBuffer;
    bool m_Visible = true;
    bool m_Dirty = true;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxUIElement)
        GEM_INTERFACE_ENTRY(XGfxUITextElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XNamedElement)
    END_GEM_INTERFACE_MAP()

    CUITextElement() = default;
    CUITextElement(XCanvas* pCanvas) : TCanvasElement(pCanvas) {}
    void Initialize() {}
    void Uninitialize() {}

    // XGfxUIElement
    GEMMETHOD_(UIElementType, GetType)() const override { return UIElementType::Text; }
    GEMMETHOD_(bool, IsVisible)() const override { return m_Visible; }
    GEMMETHOD_(void, SetVisible)(bool visible) override { m_Visible = visible; }
    GEMMETHOD_(XGfxUIGraphNode*, GetAttachedNode)() override { return m_pAttachedNode; }
    GEMMETHOD_(const GfxResourceAllocation&, GetVertexBuffer)() const override { return m_VertexBuffer; }
    GEMMETHOD_(void, SetVertexBuffer)(const GfxResourceAllocation& buffer) override { m_VertexBuffer = buffer; }

    // XGfxUITextElement
    GEMMETHOD_(void, SetText)(PCSTR utf8Text) override;
    GEMMETHOD_(PCSTR, GetText)() const override { return m_Text.c_str(); }
    GEMMETHOD_(void, SetFont)(XFont* pFont) override;
    GEMMETHOD_(void, SetLayoutConfig)(const TextLayoutConfig& config) override;
    GEMMETHOD_(const TextLayoutConfig&, GetLayoutConfig)() const override { return m_Config; }
    GEMMETHOD_(XGfxSurface*, GetGlyphAtlasTexture)() override;

    // Internal (accessed by CUIGraph during walk)
    void SetAttachedNode(XGfxUIGraphNode* pNode) { m_pAttachedNode = pNode; }
    bool IsDirty() const { return m_Dirty; }
    void ClearDirty() { m_Dirty = false; }
    void RegenerateVertices();
    uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_CachedVertices.size()); }
    const void* GetVertexData() const { return m_CachedVertices.data(); }
    bool HasContent() const { return !m_Text.empty(); }

    void SetGlyphAtlasInternal(CGlyphAtlasImpl* pAtlas);
    CGlyphAtlasImpl* GetGlyphAtlas() const { return m_pAtlas; }
};

//================================================================================================
// CUIRectElement - Rectangle element with cached vertex generation
//================================================================================================

class CUIRectElement : public TCanvasElement<XGfxUIRectElement>
{
    Math::FloatVector4 m_FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Math::FloatVector2 m_Size = {};
    std::vector<TextVertex> m_CachedVertices;

    XGfxUIGraphNode* m_pAttachedNode = nullptr;
    GfxResourceAllocation m_VertexBuffer;
    bool m_Visible = true;
    bool m_Dirty = true;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxUIElement)
        GEM_INTERFACE_ENTRY(XGfxUIRectElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XNamedElement)
    END_GEM_INTERFACE_MAP()

    CUIRectElement() = default;
    CUIRectElement(XCanvas* pCanvas) : TCanvasElement(pCanvas) {}
    void Initialize() {}
    void Uninitialize() {}

    // XGfxUIElement
    GEMMETHOD_(UIElementType, GetType)() const override { return UIElementType::Rect; }
    GEMMETHOD_(bool, IsVisible)() const override { return m_Visible; }
    GEMMETHOD_(void, SetVisible)(bool visible) override { m_Visible = visible; }
    GEMMETHOD_(XGfxUIGraphNode*, GetAttachedNode)() override { return m_pAttachedNode; }
    GEMMETHOD_(const GfxResourceAllocation&, GetVertexBuffer)() const override { return m_VertexBuffer; }
    GEMMETHOD_(void, SetVertexBuffer)(const GfxResourceAllocation& buffer) override { m_VertexBuffer = buffer; }

    // XGfxUIRectElement
    GEMMETHOD_(void, SetSize)(const Math::FloatVector2& size) override;
    GEMMETHOD_(const Math::FloatVector2&, GetSize)() const override { return m_Size; }
    GEMMETHOD_(void, SetFillColor)(const Math::FloatVector4& color) override;
    GEMMETHOD_(const Math::FloatVector4&, GetFillColor)() const override { return m_FillColor; }

    // Internal
    void SetAttachedNode(XGfxUIGraphNode* pNode) { m_pAttachedNode = pNode; }
    bool IsDirty() const { return m_Dirty; }
    void ClearDirty() { m_Dirty = false; }
    void RegenerateVertices();
    uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_CachedVertices.size()); }
    const void* GetVertexData() const { return m_CachedVertices.data(); }
    bool HasContent() const { return m_Size.X > 0.0f && m_Size.Y > 0.0f; }
};

// Concrete-type cast helpers (used by CUIGraph during tree walk)
inline CUITextElement* AsText(XGfxUIElement* p) { return static_cast<CUITextElement*>(static_cast<XGfxUITextElement*>(p)); }
inline CUIRectElement* AsRect(XGfxUIElement* p) { return static_cast<CUIRectElement*>(static_cast<XGfxUIRectElement*>(p)); }

} // namespace Canvas

#ifdef _MSC_VER
#pragma warning(pop)
#endif
