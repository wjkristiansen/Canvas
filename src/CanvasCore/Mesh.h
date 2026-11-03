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
    // The skeleton instance holds no reference back to the mesh, so a strong reference
    // here forms no cycle.
    Gem::TGemPtr<XSkeletonInstance>             m_pSkinSkeleton;
    std::vector<uint32_t>                       m_SkinJointIndices;
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
            if (!pDesc->pSkeleton || !pDesc->pJointIndices || !pDesc->pInvBindPoses)
                return Gem::Result::BadPointer;
            // Every bone must name a joint of the skeleton.  Rendering reads each bone's
            // joint unconditionally, so an out-of-range joint is rejected here rather than
            // allowed to produce a degenerate bone matrix later.
            const uint32_t jointCount = pDesc->pSkeleton->GetJointCount();
            for (uint32_t i = 0; i < pDesc->BoneCount; ++i)
                if (pDesc->pJointIndices[i] >= jointCount)
                    return Gem::Result::OutOfRange;
        }

        m_pSkinSkeleton = nullptr;
        m_SkinJointIndices.clear();
        m_SkinInvBindPoses.clear();
        if (pDesc && pDesc->BoneCount > 0)
        {
            m_pSkinSkeleton = pDesc->pSkeleton;
            m_SkinJointIndices.assign(pDesc->pJointIndices, pDesc->pJointIndices + pDesc->BoneCount);
            m_SkinInvBindPoses.assign(pDesc->pInvBindPoses, pDesc->pInvBindPoses + pDesc->BoneCount);
        }
        return Gem::Result::Success;
    }

    GEMMETHOD_(uint32_t, GetSkinBoneCount)() const final
    {
        return static_cast<uint32_t>(m_SkinJointIndices.size());
    }

    GEMMETHOD_(XSkeletonInstance*, GetSkinSkeleton)() const final
    {
        return m_pSkinSkeleton.Get();
    }

    GEMMETHOD_(uint32_t, GetSkinJointIndex)(uint32_t index) const final
    {
        return index < m_SkinJointIndices.size() ? m_SkinJointIndices[index] : InvalidIndex;
    }

    GEMMETHOD_(const Math::FloatMatrix4x4*, GetSkinInvBindPose)(uint32_t index) const final
    {
        return index < m_SkinInvBindPoses.size() ? &m_SkinInvBindPoses[index] : nullptr;
    }

    GEMMETHOD(ComputeSkinMatrices)(Math::FloatMatrix4x4 *pMatrices, uint32_t count) final
    {
        if (count != m_SkinJointIndices.size())
            return Gem::Result::InvalidArg;
        if (count == 0)
            return Gem::Result::Success;
        if (!pMatrices)
            return Gem::Result::BadPointer;

        // Row vectors: geometry -> joint space at bind -> skeleton space -> world.
        XSceneGraphNode *pSkeletonNode = m_pSkinSkeleton->GetAttachedNode();
        const Math::FloatMatrix4x4 skeletonToWorld = pSkeletonNode
            ? pSkeletonNode->GetGlobalMatrix()
            : Math::FloatMatrix4x4::Identity();
        for (uint32_t i = 0; i < count; ++i)
            pMatrices[i] = m_SkinInvBindPoses[i] * m_pSkinSkeleton->GetJointMatrix(m_SkinJointIndices[i]) * skeletonToWorld;
        return Gem::Result::Success;
    }
};

}
