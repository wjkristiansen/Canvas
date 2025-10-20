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
    Gem::TGemPtr<CSceneGraphNode> m_pRoot;
    CCanvas *m_pCanvas;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XScene)
    END_GEM_INTERFACE_MAP()

    CScene(CCanvas *pCanvas);

    Gem::Result Initialize();
    void Uninitialize() {}
    
public: // XScene methods
    GEMMETHOD_(XSceneGraphNode *, GetRootSceneGraphNode)();

    GEMMETHOD(Update)(float dtime) final { return m_pRoot->Update(dtime); }

    CScene *CastFrom(XScene *pXScene) { return static_cast<CScene *>(pXScene); }
};

}