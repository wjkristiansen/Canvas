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
    std::unique_ptr<CGlyphAtlasImpl> m_pAtlas;

    // Scratch buffers for Submit (reused across frames to avoid per-frame allocations)
    std::vector<CUITextElement*> m_VisibleTextElements;
    std::vector<UITextDrawCommand> m_DrawCommands;

    // Vertex slots freed by RemoveElement, processed in next Submit
    struct PendingVertexSlotFree
    {
        uint32_t StartVertex;
        uint32_t MaxVertexCount;
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

    GEMMETHOD_(XUIElement*, GetRoot)() override { return nullptr; }
    GEMMETHOD(CreateTextElement)(XUIElement* pParent, XUITextElement** ppElement) override;
    GEMMETHOD(CreateRectElement)(XUIElement* pParent, XUIRectElement** ppElement) override;
    GEMMETHOD(RemoveElement)(XUIElement* pElement) override;
    GEMMETHOD(Update)() override;
    GEMMETHOD(Submit)(XRenderQueue* pRenderQueue) override;

private:
    void UpdateElement(CUIElementCore* pElement);
    void CollectVisibleTextElements(CUIElementCore* pElement);
    static CUIElementCore* GetCore(XUIElement* pElement);
};

} // namespace Canvas
