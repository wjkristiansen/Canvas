//================================================================================================
// CUIGraph - UI element graph with dirty-tracked update and render submission
//================================================================================================

#pragma once
#include "UIElement.h"

namespace Canvas
{

class CUIGraph : public Gem::TGeneric<XUIGraph>
{
    CUIElementCore m_Root;
    std::vector<std::unique_ptr<CUIElementCore>> m_OwnedElements;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XUIGraph)
    END_GEM_INTERFACE_MAP()

    // Stub ref counting — caller owns via raw pointer or TGemPtr.
    GEMMETHOD_(unsigned long, AddRef)() override { return 1; }
    GEMMETHOD_(unsigned long, Release)() override { return 1; }
    GEMMETHOD(QueryInterface)(Gem::InterfaceId iid, void** ppObj) override
    {
        return InternalQueryInterface(iid, ppObj);
    }

    CUIGraph() = default;

    GEMMETHOD_(XUIElement*, GetRoot)() override { return nullptr; }
    GEMMETHOD(CreateTextElement)(XUIElement* pParent, XUITextElement** ppElement) override;
    GEMMETHOD(CreateRectElement)(XUIElement* pParent, XUIRectElement** ppElement) override;
    GEMMETHOD(RemoveElement)(XUIElement* pElement) override;
    GEMMETHOD(Update)() override;
    GEMMETHOD(Submit)(XRenderQueue* pRenderQueue) override;

private:
    void UpdateElement(CUIElementCore* pElement);
    void SubmitElement(CUIElementCore* pElement, XRenderQueue* pRenderQueue);
    static CUIElementCore* GetCore(XUIElement* pElement);
};

} // namespace Canvas
