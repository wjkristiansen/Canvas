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
    public Gem::CGenericBase,
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

public: // XScene methods
    GEMMETHOD_(XSceneGraphNode *, GetRootSceneGraphNode)();

public: // CScene methods
    CCanvas *GetCanvasImpl() const { return m_pCanvas; }
};

}