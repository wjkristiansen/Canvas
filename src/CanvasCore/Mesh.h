//================================================================================================
// Mesh
//================================================================================================

#pragma once

#include "SceneGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CMeshInstance :
    public TSceneGraphElement<XMeshInstance>
{
    Gem::TGemPtr<XGfxMeshData>                  m_pMeshData;
    // Weak (non-owning) bone references. The scene graph owns node lifetime
    // (parent->child is strong, the parent back-pointer is weak); a mesh
    // instance strongly co-owning bones in its own skeleton subtree would form
    // a reference cycle (e.g. a skinned mesh node parented under a bone it binds).
    std::vector<XSceneGraphNode*>               m_SkinBoneNodes;
    std::vector<Math::FloatMatrix4x4>           m_SkinInvBindPoses;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XSceneGraphElement)
        GEM_INTERFACE_ENTRY(XMeshInstance)
    END_GEM_INTERFACE_MAP()

    CMeshInstance(XCanvas *pCanvas) :
        TSceneGraphElement<XMeshInstance>(pCanvas)
    {}

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}
    
    // XSceneGraphElement methods
    GEMMETHOD(Update)(float /*dtime*/) final 
    { 
        return Gem::Result::Success; 
    }

    GEMMETHOD(NotifyNodeContextChanged)(_In_ XSceneGraphNode *pNode) final
    {
        return TSceneGraphElement<XMeshInstance>::NotifyNodeContextChanged(pNode);
    }

    // Returns the mesh data's local bounds, or an empty AABB when no
    // mesh data is bound.  Aggregating callers transform this through
    // the attached node's global matrix to obtain a world AABB.
    GEMMETHOD_(Math::AABB, GetLocalBounds)() const final
    {
        return m_pMeshData ? m_pMeshData->GetLocalBounds() : Math::AABB{};
    }

    // XMeshInstance methods
    GEMMETHOD_(XGfxMeshData *, GetMeshData)() final 
    { 
        return m_pMeshData.Get(); 
    }

    GEMMETHOD_(void, SetMeshData)(XGfxMeshData *pMesh) final
    {
        m_pMeshData = pMesh;
    }

    GEMMETHOD(SetSkinBinding)(const SkinBindingDesc *pDesc) final
    {
        // Validate before mutating so a rejected call leaves the binding intact.
        // (pDesc == nullptr or BoneCount == 0 is the documented "clear" request.)
        if (pDesc && pDesc->BoneCount > 0)
        {
            if (!pDesc->ppBoneNodes || !pDesc->pInvBindPoses)
                return Gem::Result::BadPointer;
            // Every bound bone must resolve to a real node.  Rendering dereferences each
            // bone node unconditionally, so a null entry is rejected here rather than
            // allowed to produce a degenerate (zeroed) bone matrix later.
            for (uint32_t i = 0; i < pDesc->BoneCount; ++i)
                if (!pDesc->ppBoneNodes[i])
                    return Gem::Result::InvalidArg;
        }

        m_SkinBoneNodes.clear();
        m_SkinInvBindPoses.clear();
        if (pDesc && pDesc->BoneCount > 0)
        {
            m_SkinBoneNodes.reserve(pDesc->BoneCount);
            m_SkinInvBindPoses.reserve(pDesc->BoneCount);
            for (uint32_t i = 0; i < pDesc->BoneCount; ++i)
                m_SkinBoneNodes.push_back(pDesc->ppBoneNodes[i]);
            m_SkinInvBindPoses.assign(pDesc->pInvBindPoses, pDesc->pInvBindPoses + pDesc->BoneCount);
        }
        return Gem::Result::Success;
    }

    GEMMETHOD_(uint32_t, GetSkinBoneCount)() const final
    {
        return static_cast<uint32_t>(m_SkinBoneNodes.size());
    }

    GEMMETHOD_(XSceneGraphNode*, GetSkinBoneNode)(uint32_t index) const final
    {
        return index < m_SkinBoneNodes.size() ? m_SkinBoneNodes[index] : nullptr;
    }

    GEMMETHOD_(const Math::FloatMatrix4x4*, GetSkinInvBindPose)(uint32_t index) const final
    {
        return index < m_SkinInvBindPoses.size() ? &m_SkinInvBindPoses[index] : nullptr;
    }
};

}
