//================================================================================================
// SceneGraph
//================================================================================================

#pragma once

#include "pch.h"
#include "Transform.h"
#include "Canvas.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
template<class _Base>
class TSceneGraphElement :
    public TCanvasElement<_Base>
{
    class CSceneGraphNode *m_pNode = nullptr;

public:
    TSceneGraphElement(CCanvas *pCanvas) :
        TCanvasElement<_Base>(pCanvas) {}

public:
    GEMMETHOD(AttachTo)(XSceneGraphNode *pNode) final;
    GEMMETHOD(Detach)() final;
    GEMMETHOD_(XSceneGraphNode *, GetAttachedNode)() final;
    GEMMETHOD(DispatchForRender)(XRenderQueue *pRenderQueue) final;
};

//------------------------------------------------------------------------------------------------
class 
CSceneGraphNode :
    public TCanvasElement<XSceneGraphNode>
{
protected:
    CSceneGraphNode *m_pParent = nullptr; // Weak pointer
    Gem::TGemPtr<CSceneGraphNode> m_pSibling;
    Gem::TGemPtr<CSceneGraphNode> m_pFirstChild;
    Gem::TAggregate<CTransform, CSceneGraphNode> m_Transform;

    struct SceneGraphElementPtrHash
    {
        size_t operator()(const Gem::TGemPtr<XSceneGraphElement> &e) const noexcept
        {
            return std::hash<XSceneGraphElement*>{}(e.Get());
        }
    };

    struct SceneGraphElementPtrEqual
    {
        bool operator()(const Gem::TGemPtr<XSceneGraphElement> &a, const Gem::TGemPtr<XSceneGraphElement> &b) const noexcept
        {
            return a.Get() == b.Get();
        }
    };

    std::unordered_set<Gem::TGemPtr<XSceneGraphElement>, SceneGraphElementPtrHash, SceneGraphElementPtrEqual> m_Elements;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XSceneGraphNode)
        GEM_INTERFACE_ENTRY_AGGREGATE(XTransform, &m_Transform)
    END_GEM_INTERFACE_MAP()

    CSceneGraphNode(CCanvas *pCanvas);
    
public: // XSceneGraphNode methods
    GEMMETHOD(Initialize)() override;
    GEMMETHOD_(void, Uninitialize)() override;
    
    GEMMETHOD(AddChild)(_In_ XSceneGraphNode* pChild) final;
    GEMMETHOD_(XSceneGraphNode *, GetParent)() final;
    GEMMETHOD_(XSceneGraphNode *, GetSibling)() final;
    GEMMETHOD_(XSceneGraphNode *, GetFirstChild)() final;

public:
    // CSceneGraphNode methods
    static CSceneGraphNode *CastFrom(XSceneGraphNode *pXface) { return static_cast<CSceneGraphNode *>(pXface); }

    void BindElement(XSceneGraphElement *pElement);
};

//------------------------------------------------------------------------------------------------
// Template method implementations
//------------------------------------------------------------------------------------------------
template<class _Base>
GEMMETHODIMP TSceneGraphElement<_Base>::AttachTo(XSceneGraphNode *pNode)
{
    // TODO: Implement attachment logic
    return Gem::Result::Success;
}

template<class _Base>
GEMMETHODIMP TSceneGraphElement<_Base>::Detach()
{
    // TODO: Implement detachment logic
    return Gem::Result::Success;
}

template<class _Base>
GEMMETHODIMP_(XSceneGraphNode *) TSceneGraphElement<_Base>::GetAttachedNode()
{
    // TODO: Implement get attached node logic
    return nullptr;
}

template<class _Base>
GEMMETHODIMP TSceneGraphElement<_Base>::DispatchForRender(XRenderQueue *pRenderQueue)
{
    // TODO: Implement render dispatch logic
    return Gem::Result::Success;
}

}