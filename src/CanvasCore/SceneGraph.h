//================================================================================================
// SceneGraph
//================================================================================================

#pragma once

#include "pch.h"
#include "Canvas.h"
#include "CanvasElement.h"
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
    Math::FloatQuaternion m_LocalRotation;
    Math::FloatVector4 m_LocalScale; // W is ignored
    Math::FloatVector4 m_LocalTranslation;
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
    END_GEM_INTERFACE_MAP()

    CSceneGraphNode(CCanvas *pCanvas);
    
public: // XSceneGraphNode methods
    GEMMETHOD(Initialize)() override;
    GEMMETHOD_(void, Uninitialize)() override;
    
    GEMMETHOD(AddChild)(_In_ XSceneGraphNode* pChild) final;
    GEMMETHOD_(XSceneGraphNode *, GetParent)() final;
    GEMMETHOD_(XSceneGraphNode *, GetSibling)() final;
    GEMMETHOD_(XSceneGraphNode *, GetFirstChild)() final;

    GEMMETHOD_(const Math::FloatQuaternion &, GetLocalRotation)() const final
    {
        return m_LocalRotation;
    }

    GEMMETHOD_(const Math::FloatVector4 &, GetLocalTranslation)() const final
    {
        return m_LocalTranslation;
    }

    GEMMETHOD_(const Math::FloatVector4 &, GetLocalScale)() const final
    {
        return m_LocalScale;
    }

    GEMMETHOD_(void, SetLocalRotation)(_In_ const Math::FloatQuaternion &rotation) final
    {
        m_LocalRotation = rotation;
    }

    GEMMETHOD_(void, SetLocalTranslation)(_In_ const Math::FloatVector4 &translation) final
    {
        m_LocalTranslation = translation;
    }
    GEMMETHOD_(void, SetLocalScale)(_In_ const Math::FloatVector4 &scale) final
    {
        m_LocalScale = scale;
    }

    GEMMETHOD_(const Math::FloatQuaternion, GetGlobalRotation)() const final
    {
        return Math::FloatQuaternion(0, 0, 0, 1.f); // TODO
    }

    GEMMETHOD_(const Math::FloatVector4, GetGlobalTranslation)() const final
    {
        return Math::FloatVector4(0, 0, 0, 1.f); // TODO
    }

    GEMMETHOD_(const Math::FloatMatrix4x4, GetGlobalMatrix)() const final
    {
        return Math::FloatMatrix4x4::Identity(); // TODO
    }

    GEMMETHOD(Update)(float dtime) final;

public:
    // CSceneGraphNode methods
    static CSceneGraphNode *CastFrom(XSceneGraphNode *pXface) { return static_cast<CSceneGraphNode *>(pXface); }

    void BindElement(XSceneGraphElement *pElement);
};

//------------------------------------------------------------------------------------------------
// Template method implementations
//------------------------------------------------------------------------------------------------
template<class _Base>
GEMMETHODIMP TSceneGraphElement<_Base>::AttachTo(XSceneGraphNode */*pNode*/)
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
GEMMETHODIMP TSceneGraphElement<_Base>::DispatchForRender(XRenderQueue */*pRenderQueue*/)
{
    // TODO: Implement render dispatch logic
    return Gem::Result::Success;
}

}