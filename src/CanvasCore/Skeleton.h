//================================================================================================
// Skeleton - shared skeleton data and the per-instance skeleton pose
//
// ModelSkeletonData : immutable bone table stored on CModel, shared by every instance.
//
// SkeletonPose      : bone matrices (skeleton space), composed parents first from local
//                     bone transforms, plus the composed rotation and scale used to give
//                     socket nodes a translation/rotation/scale local transform.
//
// CSkeletonInstance : one instance's pose of one skeleton, bound as a scene element to
//                     the node whose space the skeleton is in.  Its Update recomputes the pose
//                     and moves the instance's socket nodes when the bones changed.
//
// Bone matrices compose parents first, with row vectors:
//   - Bone matrix: M[j] = local[j] * M[parent(j)]; M[root] = local[root]
//   - Bone world matrix: M[j] * nodeGlobal
//================================================================================================

#pragma once

#include "pch.h"
#include "Canvas.h"
#include "CanvasElement.h"
#include "SceneGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure padded due to alignment specifier
#endif
struct ModelBoneData
{
    std::string         Name;
    uint32_t            ParentIndex = InvalidIndex;   // less than the bone's own index
    Transform           RestTransform;
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

struct ModelSkeletonData
{
    std::string                 Name;
    uint32_t                    NodeIndex = InvalidIndex;  // InvalidIndex: the instance root
    std::vector<ModelBoneData> Bones;
};

//------------------------------------------------------------------------------------------------
struct SkeletonPose
{
    std::vector<Math::FloatMatrix4x4>   Matrices;   // skeleton space
    std::vector<Math::FloatQuaternion>  Rotations;  // composed rotation, skeleton space
    std::vector<Math::FloatVector4>     Scales;     // composed per-axis scale, skeleton space

    // Composes the bones' local transforms (one per bone) parents first.
    void Compute(const ModelSkeletonData &skeleton, const Transform *pLocalTransforms);

    // A socket node's transform, relative to the skeleton's node, from the socket's local
    // transform relative to the given bone.
    // Translation is exact; rotation and scale compose like XSceneNode::GetGlobalRotation,
    // which matches the matrix when ancestor scale is uniform.
    Transform SocketTransform(uint32_t boneIndex, const Transform &localTransform) const;
};

//------------------------------------------------------------------------------------------------
class CSkeletonInstance :
    public TSceneElement<XSkeletonInstance>
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif
    struct Socket
    {
        XSceneNode    *pNode = nullptr;   // weak; written only while a child of this element's node
        uint32_t            BoneIndex = 0;
        Transform           LocalTransform;   // relative to the bone
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    // Strong reference to the model that owns the shared skeleton data (m_pData points into it).
    Gem::TGemPtr<XModel>        m_pModel;
    const ModelSkeletonData    *m_pData = nullptr;

    std::vector<Transform>      m_LocalTransforms;  // one per bone
    SkeletonPose                m_Pose;
    bool                        m_PoseDirty = true;
    bool                        m_SocketsDirty = true;
    std::vector<Socket>         m_Sockets;

    void RefreshPose();

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XSceneElement)
        GEM_INTERFACE_ENTRY(XSkeletonInstance)
    END_GEM_INTERFACE_MAP()

    explicit CSkeletonInstance(XCanvas *pCanvas) :
        TSceneElement<XSkeletonInstance>(pCanvas)
    {}

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    // Called once after construction, before the element is bound to its node.
    void Bind(XModel *pModel, const ModelSkeletonData *pData);

    // Registers a socket node; its local transform is set on the next Update.
    void AddSocket(XSceneNode *pNode, uint32_t boneIndex, const Transform &localTransform);

    // Direct bone access for the animation controller.  index must be in range.
    const Transform &LocalTransform(uint32_t index) const { return m_LocalTransforms[index]; }
    void SetLocalTransform(uint32_t index, const Transform &transform)
    {
        m_LocalTransforms[index] = transform;
        m_PoseDirty = true;
    }
    const Transform &RestTransform(uint32_t index) const { return m_pData->Bones[index].RestTransform; }

    // XSceneElement
    GEMMETHOD(Update)(float dtime) final;

    // XSkeletonInstance
    GEMMETHOD_(uint32_t, GetBoneCount)() const final
    {
        return static_cast<uint32_t>(m_LocalTransforms.size());
    }
    GEMMETHOD_(const char *, GetBoneName)(uint32_t index) const final;
    GEMMETHOD_(uint32_t, GetBoneParent)(uint32_t index) const final;
    GEMMETHOD_(uint32_t, FindBone)(const char *name) const final;
    GEMMETHOD(GetBoneLocalTransform)(uint32_t index, Transform *pTransform) const final;
    GEMMETHOD(SetBoneLocalTransform)(uint32_t index, const Transform *pTransform) final;
    GEMMETHOD_(void, ResetToRestPose)() final;
    GEMMETHOD_(Math::FloatMatrix4x4, GetBoneMatrix)(uint32_t index) final;
};

} // namespace Canvas
