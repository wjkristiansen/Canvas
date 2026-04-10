//================================================================================================
// CUIGraph - UI graph with dirty-tracked update and render submission
//================================================================================================

#pragma once
#include "UIElement.h"

namespace Canvas
{

class CUIGraph : public Gem::TGeneric<XUIGraph>
{
    Gem::TGemPtr<CUIGraphNodeImpl> m_pRootNode;
    std::unique_ptr<CGlyphAtlasImpl> m_pAtlas;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XUIGraph)
    END_GEM_INTERFACE_MAP()

    CUIGraph() = default;

    void Initialize() {}
    void Uninitialize() {}

    void SetAtlas(std::unique_ptr<CGlyphAtlasImpl> pAtlas) { m_pAtlas = std::move(pAtlas); }

    GEMMETHOD(CreateTextElement)(XUIGraphNode* pNode, XUITextElement** ppElement) override;
    GEMMETHOD(CreateRectElement)(XUIGraphNode* pNode, XUIRectElement** ppElement) override;
    GEMMETHOD(RemoveElement)(XUIElement* pElement) override;
    GEMMETHOD(CreateNode)(XUIGraphNode* pParent, XUIGraphNode** ppNode) override;
    GEMMETHOD_(XUIGraphNode*, GetRootNode)() override;
    GEMMETHOD(Update)() override;
    GEMMETHOD(SubmitRenderables)(XRenderQueue* pRenderQueue) override;

private:
    void UpdateNode(CUIGraphNodeImpl* pNode);
};

} // namespace Canvas
