//================================================================================================
// Scene
//================================================================================================

#pragma once

#include "Canvas.h"
#include "SceneGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CSceneGraph :
    public TCanvasElement<XSceneGraph>
{
private:
    Gem::TGemPtr<XGfxDevice> m_pDevice;
    Gem::TGemPtr<XSceneGraphNode> m_pRoot;
    Gem::TGemPtr<XCamera> m_pActiveCamera;
    std::vector<XSceneGraphNode*> m_TraversalStack;  // Reused across frames to avoid allocation

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XSceneGraph)
    END_GEM_INTERFACE_MAP()

    CSceneGraph(XCanvas *pCanvas, XGfxDevice *pDevice);

    Gem::Result Initialize();
    void Uninitialize() {}
    
public: // XSceneGraph methods
    GEMMETHOD_(XGfxDevice *, GetDevice)() final
    {
        return m_pDevice.Get();
    }

    GEMMETHOD_(XSceneGraphNode *, GetRootSceneGraphNode)();

    GEMMETHOD_(void, SetActiveCamera)(XCamera *pCamera) final
    {
        m_pActiveCamera = pCamera;
    }

    GEMMETHOD_(XCamera *, GetActiveCamera)() const final
    {
        return m_pActiveCamera.Get();
    }

    GEMMETHOD(Update)(float dtime) final
    {
        return m_pRoot->Update(dtime);
    }

    GEMMETHOD(SubmitRenderables)(XGfxRenderQueue *pRenderQueue) final;
};

}