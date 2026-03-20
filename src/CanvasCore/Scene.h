//================================================================================================
// Scene
//================================================================================================

#pragma once

#include "Canvas.h"
#include "SceneGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CScene :
    public TCanvasElement<XScene>
{
private:
    Gem::TGemPtr<XSceneGraphNode> m_pRoot;
    Gem::TGemPtr<XCamera> m_pActiveCamera;
    std::vector<XSceneGraphNode*> m_TraversalStack;  // Reused across frames to avoid allocation

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XScene)
    END_GEM_INTERFACE_MAP()

    CScene(XCanvas *pCanvas);

    Gem::Result Initialize();
    void Uninitialize() {}
    
public: // XScene methods
    GEMMETHOD_(XSceneGraphNode *, GetRootSceneGraphNode)();

    GEMMETHOD_(void, SetActiveCamera)(XCamera *pCamera) final
    {
        m_pActiveCamera = pCamera;
    }

    GEMMETHOD_(XCamera *, GetActiveCamera)() final
    {
        return m_pActiveCamera.Get();
    }

    GEMMETHOD(Update)(float dtime) final
    {
        return m_pRoot->Update(dtime);
    }

    GEMMETHOD(SubmitRenderables)(XRenderQueue *pRenderQueue) final;
};

}