//================================================================================================
// SceneGraph
//
// A node's global matrix is its local matrix times its parent's global matrix (row vectors).
//================================================================================================

#pragma once

#include "pch.h"
#include "Canvas.h"
#include "CanvasElement.h"
namespace Canvas
{

class CWorldObject;

//------------------------------------------------------------------------------------------------
template<class _Base>
class TSceneElement :
    public TCanvasElement<_Base>
{
    XSceneNode *m_pNode = nullptr;

public:
    TSceneElement(XCanvas *pCanvas) :
        TCanvasElement<_Base>(pCanvas) {}

public:
    GEMMETHOD(Detach)() final;
    GEMMETHOD_(XSceneNode *, GetAttachedNode)() final;
    GEMMETHOD(NotifyNodeContextChanged)(_In_ XSceneNode *pNode)
    {
        m_pNode = pNode;
        return Gem::Result::Success;
    }

    // Default returns an empty AABB.  Renderable elements (CMeshInstance, ...)
    // override with their node-local vertex envelope.
    // Non-renderable elements -- lights, cameras, ambient -- inherit
    // this default so they contribute nothing to renderable aggregates.
    GEMMETHOD_(Math::AABB, GetLocalBounds)() const
    {
        return Math::AABB{};
    }

    // Default returns an empty AABB.  Elements that affect a bounded
    // region of space (finite-range XLight, XCamera) override with
    // their node-local influence volume.  Infinite-influence elements
    // (directional / ambient lights) and pure-renderable elements
    // (XMeshInstance) inherit this default.
    GEMMETHOD_(Math::AABB, GetLocalInfluenceBounds)() const
    {
        return Math::AABB{};
    }
};

//------------------------------------------------------------------------------------------------
// Which of a scene node's cached transforms are stale.
enum class TransformDirtyFlags : uint32_t
{
    None              = 0,
    LocalMatrix       = 1 << 0,
    GlobalMatrix      = 1 << 1,
    GlobalRotation    = 1 << 2,
    All               = LocalMatrix | GlobalMatrix | GlobalRotation,
};
CANVAS_ENUM_FLAG_OPERATORS(TransformDirtyFlags)

//------------------------------------------------------------------------------------------------
class 
CSceneNode :
    public TCanvasElement<XSceneNode>
{
    // Internal wrapper node for intrusive doubly-linked list of children
    struct ChildNode
    {
        Gem::TGemPtr<XSceneNode> m_pNode;
        ChildNode* m_pPrev = nullptr;
        ChildNode* m_pNext = nullptr;
    };

protected:
    CSceneNode *m_pParent = nullptr; // Weak pointer
    ChildNode* m_pFirstChild = nullptr;   // Intrusive doubly-linked list for O(1) insertion anywhere
    ChildNode* m_pLastChild = nullptr;    // Tail pointer for O(1) AddChild
    std::unordered_map<XSceneNode*, ChildNode*> m_ChildMap;  // For O(1) lookup of wrapper given child pointer
    Transform m_LocalTransform;

    TransformDirtyFlags m_DirtyFlags = TransformDirtyFlags::All;

    Math::FloatMatrix4x4 m_LocalMatrix;
    Math::FloatQuaternion m_GlobalRotation;
    Math::FloatMatrix4x4 m_GlobalMatrix;

    std::vector<Gem::TGemPtr<XSceneElement>> m_Elements;
    Gem::TGemPtr<XAnimationController> m_pAnimationController;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XSceneNode)
    END_GEM_INTERFACE_MAP()

    CSceneNode(XCanvas *pCanvas);
    
    Gem::Result Initialize();
    void Uninitialize();    

public: // XSceneNode methods
    GEMMETHOD(AddChild)(_In_ XSceneNode* pChild) final;
    GEMMETHOD(RemoveChild)(_In_ XSceneNode* pChild) final;
    GEMMETHOD(InsertChildBefore)(_In_ XSceneNode* pChild, _In_ XSceneNode* pSibling) final;
    GEMMETHOD(InsertChildAfter)(_In_ XSceneNode* pChild, _In_ XSceneNode* pSibling) final;
    GEMMETHOD(BindElement)(_In_ XSceneElement *pElement) final;
    GEMMETHOD_(XSceneNode *, GetParent)() final;
    GEMMETHOD_(XSceneNode *, GetFirstChild)() final;
    GEMMETHOD_(XSceneNode *, GetNextChild)(_In_ XSceneNode* pCurrent) final;
    GEMMETHOD_(XSceneNode *, GetPrevChild)(_In_ XSceneNode* pCurrent) final;

    GEMMETHOD_(uint32_t, GetBoundElementCount)() final { return static_cast<uint32_t>(m_Elements.size()); }
    GEMMETHOD_(XSceneElement *, GetBoundElement)(uint32_t index) final { return m_Elements[index].Get(); }
    GEMMETHOD_(XAnimationController *, GetAnimationController)() final { return m_pAnimationController.Get(); }

    GEMMETHOD_(const Transform &, GetLocalTransform)() const final
    {
        return m_LocalTransform;
    }

    GEMMETHOD_(void, SetLocalTransform)(const Transform &transform) final
    {
        m_LocalTransform = transform;
        InvalidateTransforms(TransformDirtyFlags::LocalMatrix | TransformDirtyFlags::GlobalMatrix | TransformDirtyFlags::GlobalRotation);
    }

    GEMMETHOD_(const Math::FloatQuaternion &, GetLocalRotation)() const final
    {
        return m_LocalTransform.Rotation;
    }

    GEMMETHOD_(const Math::FloatVector4 &, GetLocalTranslation)() const final
    {
        return m_LocalTransform.Translation;
    }

    GEMMETHOD_(const Math::FloatVector4 &, GetLocalScale)() const final
    {
        return m_LocalTransform.Scale;
    }

    GEMMETHOD_(void, SetLocalRotation)(const Math::FloatQuaternion &rotation) final
    {
        m_LocalTransform.Rotation = rotation;
        // Rotation affects this node's local matrix and all global transforms
        // Children's global rotation and matrix are affected (but not their local matrices)
        InvalidateTransforms(TransformDirtyFlags::LocalMatrix | TransformDirtyFlags::GlobalMatrix | TransformDirtyFlags::GlobalRotation);
    }

    GEMMETHOD_(void, SetLocalTranslation)(_In_ const Math::FloatVector4 &translation) final
    {
        m_LocalTransform.Translation = translation;
        // Translation affects this node's local matrix and global matrix
        // Children's global matrices are affected (but not rotation or their local matrices)
        InvalidateTransforms(TransformDirtyFlags::LocalMatrix | TransformDirtyFlags::GlobalMatrix);
    }

    GEMMETHOD_(void, SetLocalScale)(_In_ const Math::FloatVector4 &scale) final
    {
        m_LocalTransform.Scale = scale;
        // Scale affects this node's local matrix and global matrix
        // Children's global matrices are affected (but not rotation or their local matrices)
        InvalidateTransforms(TransformDirtyFlags::LocalMatrix | TransformDirtyFlags::GlobalMatrix);
    }

    // Composed rotation of this node and its ancestors, ignoring scale. It matches the
    // rotation in GetGlobalMatrix when every ancestor's scale is uniform; non-uniform
    // ancestor scale under a rotated child shears the matrix, which no rotation describes.
    GEMMETHOD_(const Math::FloatQuaternion, GetGlobalRotation)() final
    {
        if (HasAnyFlag(m_DirtyFlags, TransformDirtyFlags::GlobalRotation))
        {
            if(m_pParent)
            {
                m_GlobalRotation = m_pParent->GetGlobalRotation() * m_LocalTransform.Rotation;
            }
            else
            {
                m_GlobalRotation = m_LocalTransform.Rotation;
            }
            m_DirtyFlags &= ~TransformDirtyFlags::GlobalRotation;
        }
        return m_GlobalRotation;
    }

    GEMMETHOD_(const Math::FloatVector4, GetGlobalTranslation)() final
    {
        // Row 3 of the global matrix, so ancestor rotation and scale apply exactly as
        // they do for rendering.
        return GetGlobalMatrix()[3];
    }

    GEMMETHOD_(const Math::FloatMatrix4x4, GetGlobalMatrix)() final
    {
        if (HasAnyFlag(m_DirtyFlags, TransformDirtyFlags::GlobalMatrix))
        {
            // Get local matrix from rotation and translation
            Math::FloatMatrix4x4 localMatrix = GetLocalMatrix();

            if(m_pParent)
            {
                m_GlobalMatrix = localMatrix * m_pParent->GetGlobalMatrix();
            }
            else
            {
                m_GlobalMatrix = localMatrix;
            }
            m_DirtyFlags &= ~TransformDirtyFlags::GlobalMatrix;
        }
        return m_GlobalMatrix;
    }

    GEMMETHOD_(const Math::FloatMatrix4x4, GetLocalMatrix)() final
    {
        if (HasAnyFlag(m_DirtyFlags, TransformDirtyFlags::LocalMatrix))
        {
            m_LocalMatrix = m_LocalTransform.ToMatrix();
            m_DirtyFlags &= ~TransformDirtyFlags::LocalMatrix;
        }

        return m_LocalMatrix;
    }


    // Updates this node's animation controller, then the elements bound to this node, in binding
    // order, then each child in order, leaving out the children that are world objects' root
    // nodes and their subtrees: the nodes that belong to the same world object as this node, when
    // this node is a world object's root node.  Returns the first failure, and updates nothing
    // after it.
    Gem::Result UpdateObjectNodes(float dtime);

    // The controller that instantiation puts on an instance root.
    void SetAnimationController(XAnimationController *pController) { m_pAnimationController = pController; }

    GEMMETHOD_(XWorldObject *, GetWorldObject)() final;

    // The world object whose root node this node is, or nullptr when it is none's.  Set by the
    // world object, which clears it when the node stops being its root node.
    CWorldObject *GetRootedObject() const { return m_pRootedObject; }
    void SetRootedObject(CWorldObject *pObject) { m_pRootedObject = pObject; }

    CSceneNode *GetParentNode() const { return m_pParent; }

    // Whether this node is a world's root node.  Set by the world that creates it.
    bool IsWorldRoot() const { return m_IsWorldRoot; }
    void SetWorldRoot() { m_IsWorldRoot = true; }

private:
    CWorldObject *m_pRootedObject = nullptr; // Weak pointer
    bool m_IsWorldRoot = false;

    // Helper to invalidate transforms on this node and propagate to children
    void InvalidateTransforms(TransformDirtyFlags flags);

    void UnlinkWrapper(ChildNode *pWrapper);
    Gem::Result MoveChild(XSceneNode *pChild, ChildNode *pBefore);
    void ReleaseChild(CSceneNode *pChild);
};

//------------------------------------------------------------------------------------------------
// Template method implementations
//------------------------------------------------------------------------------------------------
template<class _Base>
GEMMETHODIMP TSceneElement<_Base>::Detach()
{
    // TODO: Implement detachment logic
    return Gem::Result::Success;
}

template<class _Base>
GEMMETHODIMP_(XSceneNode *) TSceneElement<_Base>::GetAttachedNode()
{
    return m_pNode;
}

}