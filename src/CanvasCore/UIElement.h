//================================================================================================
// UIElement - UI graph nodes and element types
//
// CUIGraphNodeImpl: XUIGraphNode implementation — tree structure and screen-space position
// CUIElementState: Non-Gem base for element dirty tracking, vertex slot, visibility
// TUIElement<T>: Gem interface bridge for concrete element types
// CUITextElement: text element with cached vertex generation
// CUIRectElement: rectangle element with cached vertex generation
//================================================================================================

#pragma once
#include "Canvas.h"
#include "TextLayout.h"
#include "GlyphAtlas.h"

namespace Canvas
{

//================================================================================================
// CUIGraphNodeImpl - XUIGraphNode implementation (repurposed from CUIElementCore)
//================================================================================================

class CUIGraphNodeImpl : public Gem::TGeneric<XUIGraphNode>
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
    ChildNode* m_pMyEntry = nullptr;    // This node's entry in parent's child list

    Math::FloatVector2 m_LocalPosition = {};

    std::vector<Gem::TGemPtr<XUIElement>> m_Elements;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XUIGraphNode)
    END_GEM_INTERFACE_MAP()

    CUIGraphNodeImpl() = default;
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
    void InvalidateElementPositions();
    void InvalidateElementPositionsRecursive();
};

//================================================================================================
// CUIElementState - Non-Gem base class for element dirty tracking and vertex slot
//================================================================================================

struct VertexBufferSlot
{
    uint32_t StartVertex = 0;
    uint32_t MaxVertexCount = 0;
    bool GpuDirty = true;
};

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
    XUIGraphNode* m_pAttachedNode = nullptr;    // Weak pointer to owning node
    bool m_Visible = true;
    uint32_t m_DirtyFlags = DirtyAll;
    VertexBufferSlot m_BufferSlot;

public:
    virtual ~CUIElementState() = default;

    XUIGraphNode* GetAttachedNode() { return m_pAttachedNode; }
    void SetAttachedNode(XUIGraphNode* pNode) { m_pAttachedNode = pNode; }

    bool IsVisible() const { return m_Visible; }
    void SetVisible(bool visible) { m_Visible = visible; }

    uint32_t GetDirtyFlags() const { return m_DirtyFlags; }
    void ClearDirtyFlags(uint32_t flags) { m_DirtyFlags &= ~flags; }
    void MarkPositionDirty() { m_DirtyFlags |= DirtyPosition; }

    VertexBufferSlot& GetBufferSlot() { return m_BufferSlot; }

    virtual UIElementType GetType() const { return UIElementType::Root; }
    virtual void RegenerateVertices() {}
    virtual const void* GetCachedVertexData() const { return nullptr; }
    virtual uint32_t GetCachedVertexCount() const { return 0; }
    virtual bool HasContent() const { return false; }

    // Resolve CUIElementState* from an XUIElement*
    static CUIElementState* GetState(XUIElement* pElement);
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
    GEMMETHOD_(XUIGraphNode*, GetAttachedNode)() override { return m_pAttachedNode; }
};

//================================================================================================
// CUITextElement - Text element with cached vertex generation
//================================================================================================

class CUITextElement : public TUIElement<XUITextElement>
{
    std::string m_Text;
    XFont* m_pFont = nullptr;
    CGlyphAtlasImpl* m_pAtlas = nullptr;
    TextLayoutConfig m_Config;
    std::vector<TextVertex> m_CachedVertices;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XUIElement)
        GEM_INTERFACE_ENTRY(XUITextElement)
    END_GEM_INTERFACE_MAP()

    CUITextElement() = default;

    UIElementType GetType() const override { return UIElementType::Text; }

    GEMMETHOD_(void, SetText)(PCSTR utf8Text) override;
    GEMMETHOD_(PCSTR, GetText)() const override { return m_Text.c_str(); }
    GEMMETHOD_(void, SetFont)(XFont* pFont) override;
    GEMMETHOD_(void, SetLayoutConfig)(const TextLayoutConfig& config) override;
    GEMMETHOD_(const TextLayoutConfig&, GetLayoutConfig)() const override { return m_Config; }

    void RegenerateVertices() override;
    const void* GetCachedVertexData() const override { return m_CachedVertices.data(); }
    uint32_t GetCachedVertexCount() const override { return static_cast<uint32_t>(m_CachedVertices.size()); }
    bool HasContent() const override { return !m_Text.empty(); }

    void SetGlyphAtlasInternal(CGlyphAtlasImpl* pAtlas);
    CGlyphAtlasImpl* GetGlyphAtlas() const { return m_pAtlas; }
};

//================================================================================================
// CUIRectElement - Rectangle element with cached vertex generation
//================================================================================================

class CUIRectElement : public TUIElement<XUIRectElement>
{
    Math::FloatVector4 m_FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Math::FloatVector2 m_Size = {};
    std::vector<TextVertex> m_CachedVertices;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XUIElement)
        GEM_INTERFACE_ENTRY(XUIRectElement)
    END_GEM_INTERFACE_MAP()

    CUIRectElement() = default;

    UIElementType GetType() const override { return UIElementType::Rect; }

    GEMMETHOD_(void, SetSize)(const Math::FloatVector2& size) override;
    GEMMETHOD_(const Math::FloatVector2&, GetSize)() const override { return m_Size; }
    GEMMETHOD_(void, SetFillColor)(const Math::FloatVector4& color) override;
    GEMMETHOD_(const Math::FloatVector4&, GetFillColor)() const override { return m_FillColor; }

    void RegenerateVertices() override;
    const void* GetCachedVertexData() const override { return m_CachedVertices.data(); }
    uint32_t GetCachedVertexCount() const override { return static_cast<uint32_t>(m_CachedVertices.size()); }
    bool HasContent() const override { return m_Size.X > 0.0f && m_Size.Y > 0.0f; }
};

} // namespace Canvas
