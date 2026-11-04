//================================================================================================
// Animation - runtime clip storage and per-instance animation controller
//
// AnimTarget       : one animation target of a model: a property of a scene node's or joint's
//                    local transform, or a value the application reads.
//
// CAnimationClip   : immutable, shared clip data stored on CModel.  All instances of a model
//                    share the same clip data.  Each track drives one target, by index.
//
// CAnimationController : mutable per-instance evaluator, held by the instance root node so
//                        CSceneGraphNode::Update(dtime) drives it automatically each frame.
//                        It writes every target's value in the instance: the active clip's
//                        track for the target, or the target's value at instantiation.
//
//                        Supports immediate clip switching and linear cross-fade blending.
//                        Bind-pose restoration snapshots every target's value at instantiation
//                        time and restores it on demand (useful for editing / debugging).
//================================================================================================

#pragma once

#include "pch.h"
#include "Canvas.h"
#include "CanvasElement.h"
#include "SceneGraph.h"
#include "Skeleton.h"

namespace Canvas
{

// The property of a transform that a scene node or joint target is.
enum class TransformProperty : uint32_t
{
    Translation,
    Rotation,
    Scale,
};

constexpr bool IsJointTarget(AnimationTargetType type) noexcept
{
    return type == AnimationTargetType::JointTranslation ||
           type == AnimationTargetType::JointRotation ||
           type == AnimationTargetType::JointScale;
}

// Scene node and joint targets.
constexpr TransformProperty TargetProperty(AnimationTargetType type) noexcept
{
    switch (type)
    {
    case AnimationTargetType::SceneNodeRotation:
    case AnimationTargetType::JointRotation:     return TransformProperty::Rotation;
    case AnimationTargetType::SceneNodeScale:
    case AnimationTargetType::JointScale:        return TransformProperty::Scale;
    default:                                     return TransformProperty::Translation;
    }
}

// Every type but Value.
constexpr AnimationValueType TargetValueType(AnimationTargetType type) noexcept
{
    return TargetProperty(type) == TransformProperty::Rotation ? AnimationValueType::Quaternion : AnimationValueType::Float3;
}

//================================================================================================
// Model animation data (shared across instances)
//================================================================================================

struct AnimTarget
{
    AnimationTargetType     Type          = AnimationTargetType::SceneNodeTranslation;
    uint32_t                SkeletonIndex = InvalidIndex;  // joint targets only
    uint32_t                Index         = InvalidIndex;  // scene node and joint targets only
    AnimationValueType      ValueType     = AnimationValueType::Float3;
};

struct AnimTrack
{
    AnimationValueType      ValueType     = AnimationValueType::Float3;
    AnimationInterpolation  Interpolation = AnimationInterpolation::Linear;
    std::vector<float>      Times;          // ascending
    std::vector<float>      Values;         // packed, one value per time
    std::vector<float>      Coefficients;   // Cubic only: packed C1, C2, C3 per segment
};

struct CAnimationClip
{
    std::string                 Name;
    float                       Duration = 0.0f;    // seconds
    std::vector<AnimTrack>      Tracks;
    std::vector<uint32_t>       TrackTargets;       // parallel to Tracks: the target each drives
};

//================================================================================================
// CAnimationController - per-instance evaluator
//================================================================================================

class CAnimationController :
    public TCanvasElement<XAnimationController>
{
    //--------------------------------------------------------------------------------------------
    // A transform that targets write to: a scene node, or one joint of a skeleton instance.
    // Both pointers are non-owning; the instance's subtree owns the nodes and elements.
    //--------------------------------------------------------------------------------------------
    struct BoundTransform
    {
        XSceneGraphNode*        pNode     = nullptr;
        CSkeletonInstance*      pSkeleton = nullptr;
        uint32_t                Joint     = 0;

        Transform Get() const;
        void Set(const Transform &transform) const;
    };

    //--------------------------------------------------------------------------------------------
    // Per-target runtime binding.  Transform is InvalidIndex for a Value target.
    //--------------------------------------------------------------------------------------------
    struct BoundTarget
    {
        uint32_t                Transform = InvalidIndex;   // into m_Transforms
        TransformProperty       Property  = TransformProperty::Translation;
        AnimationValueType      ValueType = AnimationValueType::Float3;
    };

    // Strong reference to the model that owns the shared clip data.  The controller stores
    // raw pointers into the model's clip vector, so it must keep the model alive for its own
    // lifetime; an instance is otherwise free to outlive the XModel it was created from (as it
    // does for mesh/material data, which instances co-own via their own Gem references).
    Gem::TGemPtr<XModel>                m_pModel;

    // Shared clip definitions - pointer into m_pModel's vector (kept alive by m_pModel above)
    std::vector<CAnimationClip> const*  m_pClipDefs = nullptr;

    std::vector<BoundTransform>         m_Transforms;       // one per transform any target writes
    std::vector<Transform>              m_TransformScratch; // parallel to m_Transforms
    std::vector<BoundTarget>            m_Targets;          // parallel to the model's targets
    std::vector<Math::FloatVector4>     m_BindValues;       // parallel to m_Targets
    std::vector<Math::FloatVector4>     m_Values;           // parallel to m_Targets; as last applied
    std::vector<Math::FloatVector4>     m_Pose;             // parallel to m_Targets; scratch for evaluation

    uint32_t    m_ActiveClipIndex   = 0;
    float       m_Time              = 0.0f;
    bool        m_IsPlaying         = false;
    bool        m_InBindPose        = true;

    // Blend state
    std::vector<Math::FloatVector4> m_BlendFromPose;    // parallel to m_Targets; empty when not blending
    float                           m_BlendTime         = 0.0f;
    float                           m_BlendDuration     = 0.0f;

    //--------------------------------------------------------------------------------------------
    // Helpers
    //--------------------------------------------------------------------------------------------
    static Math::FloatQuaternion Slerp(
        Math::FloatQuaternion a, Math::FloatQuaternion b, float t);

    // Evaluate a track at time t into the components of out that its value type has.
    static void EvaluateTrack(const AnimTrack& track, float t, Math::FloatVector4& out);

    // The current value of target i: a bound transform's property, or a Value target's value.
    Math::FloatVector4 ReadTarget(size_t i) const;

    // Evaluate clip at t into m_Pose: each target's bind value, overwritten by the clip's tracks
    void EvaluateClipPose(const CAnimationClip& clip, float t);

    // Write values (parallel to m_Targets) to every target
    void ApplyValues(const std::vector<Math::FloatVector4>& values);

    // Apply the active clip at t to all targets (no blending)
    void ApplyClipPose(const CAnimationClip& clip, float t);

    // Blend from m_BlendFromPose toward the active clip at weight [0,1]
    void ApplyBlendedPose(float weight);

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XAnimationController)
    END_GEM_INTERFACE_MAP()

    explicit CAnimationController(XCanvas* pCanvas)
        : TCanvasElement<XAnimationController>(pCanvas)
    {}

    Gem::Result Initialize() { return Gem::Result::Success; }
    void        Uninitialize() {}

    //--------------------------------------------------------------------------------------------
    // Called once after construction, before the controller is attached to the instance root.
    //
    // pModel     : model that owns the clip data; held by a strong reference so the controller
    //              can keep the shared clips alive for its own lifetime
    // targets    : pModel's animation targets
    // clipDefs   : pModel's clip list (the controller stores raw pointers into it)
    // nodes      : the instance's scene node for each model node, by model node index
    // skeletons  : the instance's skeleton instance for each skeleton, by skeleton index
    //
    // Every target must be in range; XModelBuilder::Build checks this.
    //--------------------------------------------------------------------------------------------
    void BuildFromModel(
        XModel* pModel,
        const std::vector<AnimTarget>& targets,
        const std::vector<CAnimationClip>& clipDefs,
        const std::vector<XSceneGraphNode*>& nodes,
        const std::vector<CSkeletonInstance*>& skeletons);

    // XAnimationController
    GEMMETHOD(Update)(float dtime) final;

    // XAnimationController
    GEMMETHOD_(uint32_t, GetClipCount)() final
    {
        return m_pClipDefs ? static_cast<uint32_t>(m_pClipDefs->size()) : 0u;
    }

    GEMMETHOD_(const char *, GetClipName)(uint32_t index) final
    {
        if (!m_pClipDefs || index >= m_pClipDefs->size()) return nullptr;
        return (*m_pClipDefs)[index].Name.c_str();
    }

    GEMMETHOD_(bool, ValidateForNode)(_In_ XSceneGraphNode *pRoot) final;

    GEMMETHOD_(uint32_t, FindClip)(const char * name) final;

    GEMMETHOD(Play)(uint32_t clipIndex, float blendDuration) final;
    GEMMETHOD(PlayByName)(const char * clipName, float blendDuration) final;
    GEMMETHOD_(void, Stop)() final;
    GEMMETHOD_(void, ResetToBindPose)() final;

    GEMMETHOD_(uint32_t, GetTargetCount)() final { return static_cast<uint32_t>(m_Targets.size()); }
    GEMMETHOD(GetTargetValue)(uint32_t targetIndex, _Out_ float *pValue, uint32_t count) final;

    GEMMETHOD_(uint32_t, GetActiveClipIndex)() final { return m_ActiveClipIndex; }
    GEMMETHOD_(float,    GetTime)() final { return m_Time; }
    GEMMETHOD_(void,     SetTime)(float seconds) final { m_Time = seconds; }
    GEMMETHOD_(bool,     IsPlaying)() final { return m_IsPlaying; }
};

} // namespace Canvas
