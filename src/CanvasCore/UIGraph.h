//================================================================================================
// CUIGraph - UI graph with dirty-tracked update and render submission
//================================================================================================

#pragma once
#include "UIElement.h"

namespace Canvas
{

class CUIGraph : public TCanvasElement<XUIGraph>
{
    Gem::TGemPtr<CUIGraphNodeImpl> m_pRootNode;
    Gem::TGemPtr<XGfxDevice> m_pDevice;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XUIGraph)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XNamedElement)
    END_GEM_INTERFACE_MAP()

    CUIGraph() = default;
    CUIGraph(XCanvas* pCanvas) : TCanvasElement(pCanvas) {}

    void Initialize() {}
    void Uninitialize() {}

    void SetDevice(XGfxDevice* pDevice) { m_pDevice = pDevice; }

    GEMMETHOD_(XGfxDevice*, GetDevice)() override { return m_pDevice.Get(); }
    GEMMETHOD(RemoveElement)(XUIElement* pElement) override;
    GEMMETHOD(CreateNode)(XUIGraphNode* pParent, XUIGraphNode** ppNode) override;
    GEMMETHOD_(XUIGraphNode*, GetRootNode)() override;
    GEMMETHOD(Update)() override;
    GEMMETHOD(SubmitRenderables)(XGfxRenderQueue* pRenderQueue) override;

private:
    Gem::Result UpdateNode(CUIGraphNodeImpl* pNode);
};

} // namespace Canvas
