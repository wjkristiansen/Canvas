//================================================================================================
// UIElement - Base class, Gem-interface template, and concrete UI element types
//
// CUIElementCore: parent-child hierarchy, position, visibility, dirty tracking
// TUIElement<T>: Gem QueryInterface bridge for concrete element types
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
// CUIElementCore - Non-Gem base class for hierarchy and dirty tracking
//================================================================================================

class CUIElementCore
{
public:
    struct ChildNode
    {
        Gem::TGemPtr<XUIElement> pElement;
        ChildNode* pPrev = nullptr;
        ChildNode* pNext = nullptr;
    };

    enum DirtyFlags : uint32_t
    {
        DirtyNone = 0,
        DirtyContent = 1 << 0,
        DirtyPosition = 1 << 1,
        DirtyVisibility = 1 << 2,
        DirtyAll = DirtyContent | DirtyPosition | DirtyVisibility,
    };

    struct VertexBufferSlot
    {
        uint32_t StartVertex = 0;
        uint32_t MaxVertexCount = 0;
        bool GpuDirty = true;
    };

protected:
    CUIElementCore* m_pParent = nullptr;
    ChildNode* m_pFirstChild = nullptr;

    Math::FloatVector2 m_Position = {};
    Math::FloatVector2 m_AbsolutePosition = {};
    bool m_Visible = true;
    uint32_t m_DirtyFlags = DirtyAll;
    VertexBufferSlot m_BufferSlot;

public:
    CUIElementCore();
    virtual ~CUIElementCore();

    void AddChild(XUIElement* pChild);
    void RemoveChild(XUIElement* pChild);
    void RemoveFromParent();

    CUIElementCore* GetParentCore() { return m_pParent; }
    CUIElementCore* GetFirstChildCore();
    CUIElementCore* GetNextSiblingCore();

    const Math::FloatVector2& GetPosition() const { return m_Position; }
    void SetPosition(const Math::FloatVector2& position);
    const Math::FloatVector2& GetAbsolutePosition();

    bool IsVisible() const { return m_Visible; }
    void SetVisible(bool visible);
    bool IsEffectivelyVisible() const;

    uint32_t GetDirtyFlags() const { return m_DirtyFlags; }
    void ClearDirtyFlags(uint32_t flags) { m_DirtyFlags &= ~flags; }

    virtual UIElementType GetType() const { return UIElementType::Root; }
    virtual XUIElement* GetInterface() { return nullptr; }
    virtual void RegenerateVertices() {}
    virtual const void* GetCachedVertexData() const { return nullptr; }
    virtual uint32_t GetCachedVertexCount() const { return 0; }

    VertexBufferSlot& GetBufferSlot() { return m_BufferSlot; }

    // Resolve CUIElementCore* from an XUIElement* (static helper, mirrors CUIGraph::GetCore)
    static CUIElementCore* GetCore(XUIElement* pElement);

private:
    ChildNode* FindChildNode(XUIElement* pChild);
    void InvalidatePosition();
    void InvalidateVisibility();
    void RecomputeAbsolutePosition();
};

//================================================================================================
// TUIElement - Gem-interface template for concrete UI element types
//================================================================================================

template<class TInterface>
class TUIElement : public Gem::TGeneric<TInterface>, public CUIElementCore
{
public:
    void Initialize() {}
    void Uninitialize() {}

    GEMMETHOD_(UIElementType, GetType)() const override { return CUIElementCore::GetType(); }
    XUIElement* GetInterface() override { return static_cast<TInterface*>(this); }
    GEMMETHOD_(const Math::FloatVector2&, GetPosition)() const override { return CUIElementCore::GetPosition(); }
    GEMMETHOD_(void, SetPosition)(const Math::FloatVector2& position) override { CUIElementCore::SetPosition(position); }
    GEMMETHOD_(bool, IsVisible)() const override { return CUIElementCore::IsVisible(); }
    GEMMETHOD_(void, SetVisible)(bool visible) override { CUIElementCore::SetVisible(visible); }
    GEMMETHOD_(XUIElement*, GetParent)() override { return nullptr; }
    GEMMETHOD_(XUIElement*, GetFirstChild)() override { return nullptr; }
    GEMMETHOD_(XUIElement*, GetNextSibling)() override { return nullptr; }
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

    void SetGlyphAtlasInternal(CGlyphAtlasImpl* pAtlas);
    CGlyphAtlasImpl* GetGlyphAtlas() const { return m_pAtlas; }
};

//================================================================================================
// CUIRectElement - Rectangle element with cached vertex generation
//================================================================================================

class CUIRectElement : public TUIElement<XUIRectElement>
{
    Math::FloatVector2 m_Size = {};
    uint32_t m_FillColor = 0xFFFFFFFF;
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
    GEMMETHOD_(void, SetFillColor)(uint32_t color) override;
    GEMMETHOD_(uint32_t, GetFillColor)() const override { return m_FillColor; }

    void RegenerateVertices() override;
    const void* GetCachedVertexData() const override { return m_CachedVertices.data(); }
    uint32_t GetCachedVertexCount() const override { return static_cast<uint32_t>(m_CachedVertices.size()); }
};

} // namespace Canvas
