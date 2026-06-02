//================================================================================================
// Scene
//================================================================================================

#pragma once

#include "Canvas.h"
#include "SceneGraph.h"
#include "SceneBVH.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CScene :
    public TCanvasElement<XScene>
{
private:
    Gem::TGemPtr<XGfxDevice> m_pDevice;
    Gem::TGemPtr<XSceneGraphNode> m_pRoot;
    Gem::TGemPtr<XCamera> m_pActiveCamera;
    std::vector<XSceneGraphNode*> m_TraversalStack;  // Reused across frames to avoid allocation

    // BVH over the world-space AABB of every renderable scene element
    // bound to a node in the scene graph (one primitive per
    // [node, element] pair whose element returns a non-empty
    // GetLocalBounds()).  Non-renderable elements (lights, cameras)
    // honor the empty-AABB contract and contribute no primitives; they
    // continue to flow through SubmitRenderables unfiltered.  Built
    // explicitly via BuildBVH(), or lazily on the first
    // SubmitRenderables when not yet built.  See SceneBVH for the
    // current scope and capabilities.
    SceneBVH m_SceneBVH;
    mutable std::unordered_set<XSceneGraphNode*> m_VisibleNodeScratch; // reused per frame

    // Background storage.  Every scene has a background; default-constructed
    // GfxBackgroundDesc is opaque black with no cubemap.  pSkyboxCubemapA/B
    // raw observer pointers in m_Background are kept alive by the strong
    // refs m_pSkyboxCubemapA/B; setter populates both together.
    GfxBackgroundDesc                 m_Background;
    Gem::TGemPtr<XGfxSurface>         m_pSkyboxCubemapA;
    Gem::TGemPtr<XGfxSurface>         m_pSkyboxCubemapB;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XScene)
    END_GEM_INTERFACE_MAP()

    CScene(XCanvas *pCanvas, XGfxDevice *pDevice);

    Gem::Result Initialize();
    void Uninitialize() {}
    
public: // XScene methods
    GEMMETHOD_(XGfxDevice *, GetDevice)() final
    {
        return m_pDevice.Get();
    }

    GEMMETHOD_(XSceneGraphNode *, GetRootNode)();

    GEMMETHOD_(void, SetActiveCamera)(XCamera *pCamera) final
    {
        m_pActiveCamera = pCamera;
    }

    GEMMETHOD_(XCamera *, GetActiveCamera)() const final
    {
        return m_pActiveCamera.Get();
    }

    GEMMETHOD_(void, SetBackground)(const GfxBackgroundDesc *pDesc) final
    {
        if (pDesc)
        {
            m_Background      = *pDesc;
            m_pSkyboxCubemapA = pDesc->pSkyboxCubemapA;
            m_pSkyboxCubemapB = pDesc->pSkyboxCubemapB;
        }
        else
        {
            m_Background      = {};
            m_pSkyboxCubemapA = nullptr;
            m_pSkyboxCubemapB = nullptr;
        }
    }

    GEMMETHOD_(const GfxBackgroundDesc *, GetBackground)() const final
    {
        return &m_Background;
    }

    GEMMETHOD(Update)(float dtime) final
    {
        return m_pRoot->Update(dtime);
    }

    GEMMETHOD(SubmitRenderables)(XGfxRenderQueue *pRenderQueue) final;

    GEMMETHOD_(void, BuildBVH)() final
    {
        m_SceneBVH.Build(m_pRoot.Get());
    }
};

}