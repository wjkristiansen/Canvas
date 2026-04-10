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

    // Scratch buffers for Submit (reused across frames)
    std::vector<CUITextElement*> m_VisibleTextElements;
    std::vector<UITextDrawCommand> m_DrawCommands;
    std::vector<CUIRectElement*> m_VisibleRectElements;
    std::vector<UIRectDrawCommand> m_RectDrawCommands;

    // Vertex slots freed by RemoveElement, processed in next Submit
    struct PendingVertexSlotFree
    {
        uint32_t StartVertex;
        uint32_t MaxVertexCount;
        UIElementType Type;
    };
    std::vector<PendingVertexSlotFree> m_PendingVertexSlotFrees;

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
    GEMMETHOD(Submit)(XRenderQueue* pRenderQueue) override;

private:
    void UpdateNode(CUIGraphNodeImpl* pNode);
    void CollectVisibleTextElements(CUIGraphNodeImpl* pNode);
    void CollectVisibleRectElements(CUIGraphNodeImpl* pNode);
};

} // namespace Canvas
