//================================================================================================
// Skeleton
//================================================================================================

#include "pch.h"
#include "Skeleton.h"

namespace Canvas
{

//================================================================================================
// SkeletonPose
//================================================================================================

void SkeletonPose::Compute(const ModelSkeletonData &skeleton, const Transform *pLocalTransforms)
{
    const size_t count = skeleton.Bones.size();
    Matrices.resize(count);
    Rotations.resize(count);
    Scales.resize(count);

    // Bones are listed parents first, so each parent is composed before its children.
    for (size_t j = 0; j < count; ++j)
    {
        const Transform &local = pLocalTransforms[j];
        const Math::FloatMatrix4x4 localMatrix = local.ToMatrix();
        const uint32_t parent = skeleton.Bones[j].ParentIndex;
        if (parent == InvalidIndex)
        {
            Matrices[j]  = localMatrix;
            Rotations[j] = local.Rotation;
            Scales[j]    = local.Scale;
        }
        else
        {
            Matrices[j]  = localMatrix * Matrices[parent];
            Rotations[j] = Rotations[parent] * local.Rotation;
            Scales[j]    = Scales[parent] * local.Scale;
        }
    }
}

Transform SkeletonPose::SocketTransform(uint32_t boneIndex, const Transform &localTransform) const
{
    const Math::FloatMatrix4x4 m = localTransform.ToMatrix() * Matrices[boneIndex];

    Transform t;
    t.Translation = m[3];
    t.Rotation    = Rotations[boneIndex] * localTransform.Rotation;
    t.Scale       = Scales[boneIndex] * localTransform.Scale;
    return t;
}

//================================================================================================
// CSkeletonInstance
//================================================================================================

void CSkeletonInstance::Bind(XModel *pModel, const ModelSkeletonData *pData)
{
    m_pModel = pModel;      // keep the shared skeleton data alive for the instance's lifetime
    m_pData  = pData;
    m_LocalTransforms.clear();
    m_LocalTransforms.reserve(pData->Bones.size());
    for (const ModelBoneData &bone : pData->Bones)
        m_LocalTransforms.push_back(bone.RestTransform);
    m_Sockets.clear();
    m_PoseDirty    = true;
    m_SocketsDirty = true;
}

void CSkeletonInstance::AddSocket(XSceneNode *pNode, uint32_t boneIndex, const Transform &localTransform)
{
    Socket socket;
    socket.pNode          = pNode;
    socket.BoneIndex      = boneIndex;
    socket.LocalTransform = localTransform;
    m_Sockets.push_back(socket);
    m_SocketsDirty = true;
}

void CSkeletonInstance::RefreshPose()
{
    if (!m_PoseDirty || !m_pData)
        return;
    m_Pose.Compute(*m_pData, m_LocalTransforms.data());
    m_PoseDirty    = false;
    m_SocketsDirty = true;
}

GEMMETHODIMP CSkeletonInstance::Update(float /*dtime*/)
{
    RefreshPose();
    XSceneNode *pNode = GetAttachedNode();
    if (m_SocketsDirty && pNode)
    {
        // Socket pointers are weak: write only the sockets that are still children of this
        // element's node, which keeps them alive.
        for (XSceneNode *pChild = pNode->GetFirstChild(); pChild; pChild = pNode->GetNextChild(pChild))
        {
            for (const Socket &socket : m_Sockets)
            {
                if (socket.pNode != pChild)
                    continue;
                pChild->SetLocalTransform(m_Pose.SocketTransform(socket.BoneIndex, socket.LocalTransform));
            }
        }
        m_SocketsDirty = false;
    }
    return Gem::Result::Success;
}

GEMMETHODIMP_(const char *) CSkeletonInstance::GetBoneName(uint32_t index) const
{
    return (m_pData && index < m_pData->Bones.size()) ? m_pData->Bones[index].Name.c_str() : nullptr;
}

GEMMETHODIMP_(uint32_t) CSkeletonInstance::GetBoneParent(uint32_t index) const
{
    return (m_pData && index < m_pData->Bones.size()) ? m_pData->Bones[index].ParentIndex : InvalidIndex;
}

GEMMETHODIMP_(uint32_t) CSkeletonInstance::FindBone(const char *name) const
{
    if (!m_pData || !name)
        return InvalidIndex;
    for (size_t j = 0; j < m_pData->Bones.size(); ++j)
        if (m_pData->Bones[j].Name == name)
            return static_cast<uint32_t>(j);
    return InvalidIndex;
}

GEMMETHODIMP CSkeletonInstance::GetBoneLocalTransform(uint32_t index, Transform *pTransform) const
{
    if (!pTransform)
        return Gem::Result::BadPointer;
    if (index >= m_LocalTransforms.size())
        return Gem::Result::InvalidArg;
    *pTransform = m_LocalTransforms[index];
    return Gem::Result::Success;
}

GEMMETHODIMP CSkeletonInstance::SetBoneLocalTransform(uint32_t index, const Transform *pTransform)
{
    if (!pTransform)
        return Gem::Result::BadPointer;
    if (index >= m_LocalTransforms.size())
        return Gem::Result::InvalidArg;
    SetLocalTransform(index, *pTransform);
    return Gem::Result::Success;
}

GEMMETHODIMP_(void) CSkeletonInstance::ResetToRestPose()
{
    if (!m_pData)
        return;
    for (size_t j = 0; j < m_LocalTransforms.size(); ++j)
        m_LocalTransforms[j] = m_pData->Bones[j].RestTransform;
    m_PoseDirty = true;
}

GEMMETHODIMP_(Math::FloatMatrix4x4) CSkeletonInstance::GetBoneMatrix(uint32_t index)
{
    RefreshPose();
    return index < m_Pose.Matrices.size() ? m_Pose.Matrices[index] : Math::FloatMatrix4x4::Identity();
}

} // namespace Canvas
