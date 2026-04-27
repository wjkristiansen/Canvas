//================================================================================================
// UIElement - UI graph node implementation
//
// CUIGraphNodeImpl: XUIGraphNode implementation — tree structure and screen-space position
//================================================================================================

#pragma once
#include "Canvas.h"
#include "CanvasElement.h"

namespace Canvas
{

//================================================================================================
// CUIGraphNodeImpl - XUIGraphNode implementation
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
    ChildNode* m_pLastChild = nullptr;
    ChildNode* m_pMyEntry = nullptr;

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

} // namespace Canvas
