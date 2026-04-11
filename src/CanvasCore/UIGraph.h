//================================================================================================
// CUIGraph - UI graph with dirty-tracked update and render submission
//================================================================================================

#pragma once
#include "UIElement.h"

namespace Canvas
{

class CUIGraph : public Gem::TGeneric<XGfxUIGraph>
{
    Gem::TGemPtr<CUIGraphNodeImpl> m_pRootNode;
    std::unique_ptr<CGlyphAtlasImpl> m_pAtlas;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XGfxUIGraph)
    END_GEM_INTERFACE_MAP()

    CUIGraph() = default;

    void Initialize() {}
    void Uninitialize() {}

    void SetAtlas(std::unique_ptr<CGlyphAtlasImpl> pAtlas) { m_pAtlas = std::move(pAtlas); }

    GEMMETHOD(CreateTextElement)(XGfxUIGraphNode* pNode, XGfxUITextElement** ppElement) override;
    GEMMETHOD(CreateRectElement)(XGfxUIGraphNode* pNode, XGfxUIRectElement** ppElement) override;
    GEMMETHOD(RemoveElement)(XGfxUIElement* pElement) override;
    GEMMETHOD(CreateNode)(XGfxUIGraphNode* pParent, XGfxUIGraphNode** ppNode) override;
    GEMMETHOD_(XGfxUIGraphNode*, GetRootNode)() override;
    GEMMETHOD(Update)() override;
    GEMMETHOD(SubmitRenderables)(XRenderQueue* pRenderQueue) override;

private:
    void UpdateNode(CUIGraphNodeImpl* pNode);
};

} // namespace Canvas
