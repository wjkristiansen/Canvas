//================================================================================================
// SceneGraph
//
// MATRIX CONVENTION: Canvas uses ROW VECTORS (v' = v * M)
//   - Translation is in BOTTOM ROW: matrix[3][0], matrix[3][1], matrix[3][2]
//   - Matrix multiplication: world = parent * local (not local * parent)
//   - Transform hierarchy: child inherits parent transform by multiplying parent * child
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

    // Dirty flags bitfield
    enum DirtyFlags : uint32_t
    {
        DirtyLocalMatrix = 1 << 0,
        DirtyGlobalMatrix = 1 << 1,
        DirtyGlobalRotation = 1 << 2,
        DirtyGlobalTranslation = 1 << 3,
        DirtyAll = DirtyLocalMatrix | DirtyGlobalMatrix | DirtyGlobalRotation | DirtyGlobalTranslation
    };
    uint32_t m_DirtyFlags = DirtyAll;

    Math::FloatMatrix4x4 m_LocalMatrix;
    Math::FloatQuaternion m_GlobalRotation;
    Math::FloatVector4 m_GlobalTranslation;
    Math::FloatMatrix4x4 m_GlobalMatrix;
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
        // Rotation affects this node's local matrix and all global transforms
        // Children's global rotation, translation, and matrix are affected (but not their local matrices)
        InvalidateTransforms(DirtyLocalMatrix | DirtyGlobalMatrix | DirtyGlobalRotation | DirtyGlobalTranslation);
    }

    GEMMETHOD_(void, SetLocalTranslation)(_In_ const Math::FloatVector4 &translation) final
    {
        m_LocalTranslation = translation;
        // Translation affects this node's local matrix and global translation/matrix
        // Children's global translation and matrix are affected (but not rotation or their local matrices)
        InvalidateTransforms(DirtyLocalMatrix | DirtyGlobalMatrix | DirtyGlobalTranslation);
    }

    GEMMETHOD_(void, SetLocalScale)(_In_ const Math::FloatVector4 &scale) final
    {
        m_LocalScale = scale;
        // Scale affects this node's local matrix and global translation/matrix
        // Children's global translation and matrix are affected (but not rotation or their local matrices)
        InvalidateTransforms(DirtyLocalMatrix | DirtyGlobalMatrix | DirtyGlobalTranslation);
    }

    GEMMETHOD_(const Math::FloatQuaternion, GetGlobalRotation)() final
    {
        if(m_DirtyFlags & DirtyGlobalRotation)
        {
            if(m_pParent)
            {
                m_GlobalRotation = m_pParent->GetGlobalRotation() * m_LocalRotation;
            }
            else
            {
                m_GlobalRotation =  m_LocalRotation;
            }
            m_DirtyFlags &= ~DirtyGlobalRotation;
        }
        return m_GlobalRotation;
    }

    GEMMETHOD_(const Math::FloatVector4, GetGlobalTranslation)() final
    {
        if(m_DirtyFlags & DirtyGlobalTranslation)
        {
            if(m_pParent)
            {
                const auto parentRotation = m_pParent->GetGlobalRotation();
                const auto parentTranslation = m_pParent->GetGlobalTranslation();

                // Rotate local translation by parent's rotation using pure quaternion
                const Math::FloatQuaternion vLocal(m_LocalTranslation.X, m_LocalTranslation.Y, m_LocalTranslation.Z, 0.0f);
                const auto qConj = Conjugate(parentRotation);
                const auto vRot = parentRotation * vLocal * qConj;
                Math::FloatVector4 rotatedLocal(vRot.X, vRot.Y, vRot.Z, 0.0f);

                // Add to parent translation (row-vector convention)
                m_GlobalTranslation = parentTranslation + rotatedLocal;
                // Preserve homogeneous coordinate
                m_GlobalTranslation.W = parentTranslation.W;
            }
            else
            {
                m_GlobalTranslation = m_LocalTranslation;
            }
            m_DirtyFlags &= ~DirtyGlobalTranslation;
        }
        return m_GlobalTranslation;
    }

    GEMMETHOD_(const Math::FloatMatrix4x4, GetGlobalMatrix)() final
    {
        if(m_DirtyFlags & DirtyGlobalMatrix)
        {
            // Get local matrix from rotation and translation
            Math::FloatMatrix4x4 localMatrix = GetLocalMatrix();

            if(m_pParent)
            {
                m_GlobalMatrix = m_pParent->GetGlobalMatrix() * localMatrix;
            }
            else
            {
                m_GlobalMatrix = localMatrix;
            }
            m_DirtyFlags &= ~DirtyGlobalMatrix;
        }
        return m_GlobalMatrix;
    }

    GEMMETHOD_(const Math::FloatMatrix4x4, GetLocalMatrix)() final
    {
        if(m_DirtyFlags & DirtyLocalMatrix)
        {
            // Set affine matrix 3x3 rotation components from local rotation quaternion
            m_LocalMatrix = Math::QuaternionToRotationMatrix(m_LocalRotation);

            // CRITICAL: Canvas uses ROW VECTORS (v' = v * M)
            // Translation MUST be in BOTTOM ROW [3][col], NOT right column [row][3]
            m_LocalMatrix[3][0] = m_LocalTranslation.X;
            m_LocalMatrix[3][1] = m_LocalTranslation.Y;
            m_LocalMatrix[3][2] = m_LocalTranslation.Z;
            m_LocalMatrix[3][3] = 1.0f;  // Homogeneous coordinate

            m_DirtyFlags &= ~DirtyLocalMatrix;
        }

        return m_LocalMatrix;
    }


    GEMMETHOD(Update)(float dtime) final;

public:
    // CSceneGraphNode methods
    static CSceneGraphNode *CastFrom(XSceneGraphNode *pXface) { return static_cast<CSceneGraphNode *>(pXface); }

    void BindElement(XSceneGraphElement *pElement);
    
private:
    // Helper to invalidate transforms on this node and propagate to children
    void InvalidateTransforms(uint32_t flags);
};

//------------------------------------------------------------------------------------------------
// Template method implementations
//------------------------------------------------------------------------------------------------
template<class _Base>
GEMMETHODIMP TSceneGraphElement<_Base>::AttachTo(XSceneGraphNode *pNode)
{
    if (!pNode)
        return Gem::Result::InvalidArg;

    CSceneGraphNode* pSceneNode = CSceneGraphNode::CastFrom(pNode);
    if (!pSceneNode)
        return Gem::Result::InvalidArg;

    // Add this element to the node's elements set
    pSceneNode->BindElement(this);
    m_pNode = pSceneNode;

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
    return m_pNode;
}

template<class _Base>
GEMMETHODIMP TSceneGraphElement<_Base>::DispatchForRender(XRenderQueue */*pRenderQueue*/)
{
    // TODO: Implement render dispatch logic
    return Gem::Result::Success;
}

}