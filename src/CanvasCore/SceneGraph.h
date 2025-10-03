//================================================================================================
// SceneGraph
//================================================================================================

#pragma once

#include "pch.h"
#include "Canvas.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class 
CSceneGraphNode :
    public Gem::CGenericBase,
    public TCanvasElement<XSceneGraphNode>
{
protected:
    CSceneGraphNode *m_pParent = nullptr; // Weak pointer
    Gem::TGemPtr<CSceneGraphNode> m_pSibling;
    Gem::TGemPtr<CSceneGraphNode> m_pFirstChild;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XSceneGraphNode)
    END_GEM_INTERFACE_MAP()

    CSceneGraphNode(CCanvas *pCanvas);

    GEMMETHOD(AddChild)(_In_ XSceneGraphNode* pChild) final;
    GEMMETHOD_(XSceneGraphNode *, GetParent)() final;
    GEMMETHOD_(XSceneGraphNode *, GetSibling)() final;
    GEMMETHOD_(XSceneGraphNode *, GetFirstChild)() final;

    GEMMETHOD_(void, SetTransform)(XTransform *pTransform) final;
    GEMMETHOD_(XTransform *, GetTransform)() const final;

    GEMMETHOD_(void, SetRenderable)(XRenderable *pRenderable) final;
    GEMMETHOD_(XRenderable *, GetMesh)() final;

    GEMMETHOD_(void, SetCamera)(XCamera *pMesh) final;
    GEMMETHOD_(XCamera *, GetCamera)() final;

    GEMMETHOD_(void, SetLight)(XLight *pMesh) final;
    GEMMETHOD_(XLight *, GetLight)() final;
};

}