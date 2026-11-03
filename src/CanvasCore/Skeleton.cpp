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
    const size_t count = skeleton.Joints.size();
    Matrices.resize(count);
    Rotations.resize(count);
    Scales.resize(count);

    // Joints are listed parents first, so each parent is composed before its children.
    for (size_t j = 0; j < count; ++j)
    {
        const Transform &local = pLocalTransforms[j];
        const Math::FloatMatrix4x4 localMatrix = ComposeLocalMatrix(local);
        const uint32_t parent = skeleton.Joints[j].ParentIndex;
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

Transform SkeletonPose::SocketTransform(uint32_t jointIndex, const Transform &localTransform) const
{
    const Math::FloatMatrix4x4 m = ComposeLocalMatrix(localTransform) * Matrices[jointIndex];

    Transform t;
    t.Translation = Math::FloatVector4(m[3][0], m[3][1], m[3][2], 0.0f);
    t.Rotation    = Rotations[jointIndex] * localTransform.Rotation;
    t.Scale       = Scales[jointIndex] * localTransform.Scale;
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
    m_LocalTransforms.reserve(pData->Joints.size());
    for (const ModelJointData &joint : pData->Joints)
        m_LocalTransforms.push_back(joint.RestTransform);
    m_Sockets.clear();
    m_PoseDirty    = true;
    m_SocketsDirty = true;
}

void CSkeletonInstance::AddSocket(XSceneGraphNode *pNode, uint32_t jointIndex, const Transform &localTransform)
{
    Socket socket;
    socket.pNode          = pNode;
    socket.JointIndex     = jointIndex;
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
    XSceneGraphNode *pNode = GetAttachedNode();
    if (m_SocketsDirty && pNode)
    {
        // Socket pointers are weak: write only the sockets that are still children of this
        // element's node, which keeps them alive.
        for (XSceneGraphNode *pChild = pNode->GetFirstChild(); pChild; pChild = pNode->GetNextChild(pChild))
        {
            for (const Socket &socket : m_Sockets)
            {
                if (socket.pNode != pChild)
                    continue;
                const Transform t = m_Pose.SocketTransform(socket.JointIndex, socket.LocalTransform);
                pChild->SetLocalTranslation(t.Translation);
                pChild->SetLocalRotation(t.Rotation);
                pChild->SetLocalScale(t.Scale);
            }
        }
        m_SocketsDirty = false;
    }
    return Gem::Result::Success;
}

GEMMETHODIMP_(const char *) CSkeletonInstance::GetJointName(uint32_t index) const
{
    return (m_pData && index < m_pData->Joints.size()) ? m_pData->Joints[index].Name.c_str() : nullptr;
}

GEMMETHODIMP_(uint32_t) CSkeletonInstance::GetJointParent(uint32_t index) const
{
    return (m_pData && index < m_pData->Joints.size()) ? m_pData->Joints[index].ParentIndex : InvalidIndex;
}

GEMMETHODIMP_(uint32_t) CSkeletonInstance::FindJoint(const char *name) const
{
    if (!m_pData || !name)
        return InvalidIndex;
    for (size_t j = 0; j < m_pData->Joints.size(); ++j)
        if (m_pData->Joints[j].Name == name)
            return static_cast<uint32_t>(j);
    return InvalidIndex;
}

GEMMETHODIMP CSkeletonInstance::GetJointLocalTransform(uint32_t index, Transform *pTransform) const
{
    if (!pTransform)
        return Gem::Result::BadPointer;
    if (index >= m_LocalTransforms.size())
        return Gem::Result::InvalidArg;
    *pTransform = m_LocalTransforms[index];
    return Gem::Result::Success;
}

GEMMETHODIMP CSkeletonInstance::SetJointLocalTransform(uint32_t index, const Transform *pTransform)
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
        m_LocalTransforms[j] = m_pData->Joints[j].RestTransform;
    m_PoseDirty = true;
}

GEMMETHODIMP_(Math::FloatMatrix4x4) CSkeletonInstance::GetJointMatrix(uint32_t index)
{
    RefreshPose();
    return index < m_Pose.Matrices.size() ? m_Pose.Matrices[index] : Math::FloatMatrix4x4::Identity();
}

} // namespace Canvas
