//================================================================================================
// UIElement - UI graph nodes and element types
//
// CUIGraphNodeImpl: XGfxUIGraphNode implementation — tree structure and screen-space position
// CUIElementState: Non-Gem base for element dirty tracking, vertex buffer, visibility
// TUIElement<T>: Gem interface bridge for concrete element types
// CUITextElement: text element with cached vertex generation
// CUIRectElement: rectangle element with cached vertex generation
//================================================================================================

#pragma once
#include "Canvas.h"
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

class CUIGraphNodeImpl : public Gem::TGeneric<XGfxUIGraphNode>
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
    END_GEM_INTERFACE_MAP()

    CUIGraphNodeImpl() = default;
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
    void InvalidateElementPositions();
    void InvalidateElementPositionsRecursive();
};

//================================================================================================
// CUIElementState - Non-Gem base class for element dirty tracking and vertex buffer
//================================================================================================

class CUIElementState
{
public:
    enum DirtyFlags : uint32_t
    {
        DirtyNone = 0,
        DirtyContent = 1 << 0,
        DirtyPosition = 1 << 1,
        DirtyAll = DirtyContent | DirtyPosition,
    };

protected:
    GfxBufferSuballocation m_VertexBuffer;
    XGfxUIGraphNode* m_pAttachedNode = nullptr;    // Weak pointer to owning node
    uint32_t m_DirtyFlags = DirtyAll;
    bool m_Visible = true;

public:
    virtual ~CUIElementState() = default;

    XGfxUIGraphNode* GetAttachedNode() { return m_pAttachedNode; }
    void SetAttachedNode(XGfxUIGraphNode* pNode) { m_pAttachedNode = pNode; }

    bool IsVisible() const { return m_Visible; }
    void SetVisible(bool visible) { m_Visible = visible; }

    uint32_t GetDirtyFlags() const { return m_DirtyFlags; }
    void ClearDirtyFlags(uint32_t flags) { m_DirtyFlags &= ~flags; }
    void MarkPositionDirty() { m_DirtyFlags |= DirtyPosition; }

    virtual UIElementType GetType() const { return UIElementType::Root; }
    virtual void RegenerateVertices() {}

    // Resolve CUIElementState* from an XGfxUIElement*
    static CUIElementState* GetState(XGfxUIElement* pElement);
};

//================================================================================================
// TUIElement - Gem-interface template for concrete UI element types
//================================================================================================

template<class TInterface>
class TUIElement : public Gem::TGeneric<TInterface>, public CUIElementState
{
public:
    void Initialize() {}
    void Uninitialize() {}

    GEMMETHOD_(UIElementType, GetType)() const override { return CUIElementState::GetType(); }
    GEMMETHOD_(bool, IsVisible)() const override { return CUIElementState::IsVisible(); }
    GEMMETHOD_(void, SetVisible)(bool visible) override { CUIElementState::SetVisible(visible); }
    GEMMETHOD_(XGfxUIGraphNode*, GetAttachedNode)() override { return m_pAttachedNode; }

    // Default vertex data access (no content)
    GEMMETHOD_(uint32_t, GetVertexCount)() const override { return 0; }
    GEMMETHOD_(const void*, GetVertexData)() const override { return nullptr; }
    GEMMETHOD_(bool, HasContent)() const override { return false; }

    // GPU vertex buffer suballocation (assigned by render queue after upload)
    GEMMETHOD_(const GfxBufferSuballocation&, GetVertexBuffer)() const override { return m_VertexBuffer; }
    GEMMETHOD_(void, SetVertexBuffer)(const GfxBufferSuballocation& buffer) override { m_VertexBuffer = buffer; }
};

//================================================================================================
// CUITextElement - Text element with cached vertex generation
//================================================================================================

class CUITextElement : public TUIElement<XGfxUITextElement>
{
    std::string m_Text;
    XFont* m_pFont = nullptr;
    CGlyphAtlasImpl* m_pAtlas = nullptr;
    TextLayoutConfig m_Config;
    std::vector<TextVertex> m_CachedVertices;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxUIElement)
        GEM_INTERFACE_ENTRY(XGfxUITextElement)
    END_GEM_INTERFACE_MAP()

    CUITextElement() = default;

    UIElementType GetType() const override { return UIElementType::Text; }

    GEMMETHOD_(void, SetText)(PCSTR utf8Text) override;
    GEMMETHOD_(PCSTR, GetText)() const override { return m_Text.c_str(); }
    GEMMETHOD_(void, SetFont)(XFont* pFont) override;
    GEMMETHOD_(void, SetLayoutConfig)(const TextLayoutConfig& config) override;
    GEMMETHOD_(const TextLayoutConfig&, GetLayoutConfig)() const override { return m_Config; }

    void RegenerateVertices() override;
    GEMMETHOD_(uint32_t, GetVertexCount)() const override { return static_cast<uint32_t>(m_CachedVertices.size()); }
    GEMMETHOD_(const void*, GetVertexData)() const override { return m_CachedVertices.data(); }
    GEMMETHOD_(bool, HasContent)() const override { return !m_Text.empty(); }
    GEMMETHOD_(XGfxSurface*, GetGlyphAtlasTexture)() override;

    void SetGlyphAtlasInternal(CGlyphAtlasImpl* pAtlas);
    CGlyphAtlasImpl* GetGlyphAtlas() const { return m_pAtlas; }
};

//================================================================================================
// CUIRectElement - Rectangle element with cached vertex generation
//================================================================================================

class CUIRectElement : public TUIElement<XGfxUIRectElement>
{
    Math::FloatVector4 m_FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Math::FloatVector2 m_Size = {};
    std::vector<TextVertex> m_CachedVertices;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxUIElement)
        GEM_INTERFACE_ENTRY(XGfxUIRectElement)
    END_GEM_INTERFACE_MAP()

    CUIRectElement() = default;

    UIElementType GetType() const override { return UIElementType::Rect; }

    GEMMETHOD_(void, SetSize)(const Math::FloatVector2& size) override;
    GEMMETHOD_(const Math::FloatVector2&, GetSize)() const override { return m_Size; }
    GEMMETHOD_(void, SetFillColor)(const Math::FloatVector4& color) override;
    GEMMETHOD_(const Math::FloatVector4&, GetFillColor)() const override { return m_FillColor; }

    void RegenerateVertices() override;
    GEMMETHOD_(uint32_t, GetVertexCount)() const override { return static_cast<uint32_t>(m_CachedVertices.size()); }
    GEMMETHOD_(const void*, GetVertexData)() const override { return m_CachedVertices.data(); }
    GEMMETHOD_(bool, HasContent)() const override { return m_Size.X > 0.0f && m_Size.Y > 0.0f; }
};

} // namespace Canvas

#ifdef _MSC_VER
#pragma warning(pop)
#endif
