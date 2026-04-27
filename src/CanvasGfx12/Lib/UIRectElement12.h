//================================================================================================
// CUIRectElement12 - D3D12 implementation of XUIRectElement
//
// Rectangle element (vertex-buffer-free; geometry derived on GPU from constant data).
//================================================================================================

#pragma once

#include "CanvasGfx12.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // structure padded due to alignment specifier
#endif

class CUIRectElement12 : public TGfxElement<Canvas::XUIRectElement>
{
    Canvas::Math::FloatVector4 m_FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Canvas::Math::FloatVector2 m_Size = {};

    Canvas::XUIGraphNode* m_pAttachedNode = nullptr;
    Canvas::Math::FloatVector2 m_LocalOffset = {};
    bool m_Visible = true;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XUIElement)
        GEM_INTERFACE_ENTRY(Canvas::XUIRectElement)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CUIRectElement12() = default;
    CUIRectElement12(Canvas::XCanvas* pCanvas) : TGfxElement(pCanvas) {}
    void Initialize() {}
    void Uninitialize() {}

    // XUIElement
    GEMMETHOD_(Canvas::UIElementType, GetType)() const override { return Canvas::UIElementType::Rect; }
    GEMMETHOD_(bool, IsVisible)() const override { return m_Visible; }
    GEMMETHOD_(void, SetVisible)(bool visible) override { m_Visible = visible; }
    GEMMETHOD_(Canvas::XUIGraphNode*, GetAttachedNode)() override { return m_pAttachedNode; }
    GEMMETHOD(Detach)() override { m_pAttachedNode = nullptr; return Gem::Result::Success; }
    GEMMETHOD(NotifyNodeContextChanged)(_In_ Canvas::XUIGraphNode* pNode) override { (void)pNode; return Gem::Result::Success; }
    GEMMETHOD(Update)() override { return Gem::Result::Success; }
    GEMMETHOD_(bool, HasContent)() const override { return m_Size.X > 0.0f && m_Size.Y > 0.0f; }
    GEMMETHOD_(const Canvas::Math::FloatVector2&, GetLocalOffset)() const override { return m_LocalOffset; }
    GEMMETHOD_(void, SetLocalOffset)(const Canvas::Math::FloatVector2& offset) override { m_LocalOffset = offset; }

    // XUIRectElement
    GEMMETHOD_(void, SetSize)(const Canvas::Math::FloatVector2& size) override { m_Size = size; }
    GEMMETHOD_(const Canvas::Math::FloatVector2&, GetSize)() const override { return m_Size; }
    GEMMETHOD_(void, SetFillColor)(const Canvas::Math::FloatVector4& color) override { m_FillColor = color; }
    GEMMETHOD_(const Canvas::Math::FloatVector4&, GetFillColor)() const override { return m_FillColor; }

    // Internal
    void SetAttachedNode(Canvas::XUIGraphNode* pNode) { m_pAttachedNode = pNode; }
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
