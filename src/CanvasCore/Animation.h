//================================================================================================
// Animation - runtime clip storage and per-instance animation controller
//
// CAnimationClip   : immutable, shared clip data stored on CModel (one per FBX AnimationStack
//                    / Blender Action).  All instances of a model share the same clip data.
//                    Each track names its target by index.
//
// CAnimationController : mutable per-instance evaluator, held by the instance root node so
//                        CSceneGraphNode::Update(dtime) drives it automatically each frame.
//                        It writes each track's values onto its target in the instance.
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

//================================================================================================
// CAnimationClip - immutable clip data (shared across instances)
//================================================================================================

struct AnimTrack
{
    AnimationTargetType             TargetType    = AnimationTargetType::ModelNode;
    uint32_t                        SkeletonIndex = InvalidIndex;  // Joint targets only
    uint32_t                        TargetIndex   = InvalidIndex;
    std::vector<AnimationKeyframe>  Keyframes;  // sorted ascending by Time
};

struct CAnimationClip
{
    std::string                 Name;
    float                       Duration = 0.0f;    // seconds
    std::vector<AnimTrack>      Tracks;
};

//================================================================================================
// CAnimationController - per-instance evaluator
//================================================================================================

class CAnimationController :
    public TCanvasElement<XAnimationController>
{
    //--------------------------------------------------------------------------------------------
    // A transform target: a scene node, or one joint of a skeleton instance.
    // Both pointers are non-owning; the instance's subtree owns the nodes and elements.
    //--------------------------------------------------------------------------------------------
    struct TransformTarget
    {
        XSceneGraphNode*        pNode     = nullptr;
        CSkeletonInstance*      pSkeleton = nullptr;
        uint32_t                Joint     = 0;

        Transform Get() const;
        void Set(const Transform &transform) const;
    };

    //--------------------------------------------------------------------------------------------
    // Per-track runtime binding: target slot + keyframe data pointer
    //--------------------------------------------------------------------------------------------
    struct BoundTrack
    {
        uint32_t                              TargetIndex = 0;   // into m_Targets
        const std::vector<AnimationKeyframe>* pKeys = nullptr;   // into m_pClipDefs[i].Tracks[j].Keyframes
    };

    struct BoundClip
    {
        const CAnimationClip*       pDef = nullptr;
        std::vector<BoundTrack>     Tracks;
    };

    // Strong reference to the model that owns the shared clip data.  The controller stores
    // raw pointers into the model's clip vector (m_pClipDefs and the per-track keyframe
    // pointers below), so it must keep the model alive for its own lifetime; an instance is
    // otherwise free to outlive the XModel it was created from (as it does for mesh/material
    // data, which instances co-own via their own Gem references).
    Gem::TGemPtr<XModel>                m_pModel;

    // Shared clip definitions - pointer into m_pModel's vector (kept alive by m_pModel above)
    std::vector<CAnimationClip> const*  m_pClipDefs = nullptr;
    std::vector<BoundClip>              m_BoundClips;       // parallel to *m_pClipDefs

    std::vector<TransformTarget>        m_Targets;          // one per uniquely animated transform
    std::vector<Transform>              m_BindPose;         // parallel to m_Targets

    uint32_t    m_ActiveClipIndex   = 0;
    float       m_Time              = 0.0f;
    bool        m_IsPlaying         = false;
    bool        m_InBindPose        = true;

    // Blend state
    std::vector<Transform>      m_BlendFromPose;    // parallel to m_Targets; empty when not blending
    float                       m_BlendTime         = 0.0f;
    float                       m_BlendDuration     = 0.0f;

    //--------------------------------------------------------------------------------------------
    // Helpers
    //--------------------------------------------------------------------------------------------
    static Math::FloatQuaternion Slerp(
        Math::FloatQuaternion a, Math::FloatQuaternion b, float t);

    static void EvaluateTrack(
        const std::vector<AnimationKeyframe>& keys, float t, Transform& out);

    // Snapshot the current pose of every target into dest (used before a blend transition)
    void SnapshotPose(std::vector<Transform>& dest) const;

    // Apply the active clip at t to all bound targets (no blending)
    void ApplyClipPose(const BoundClip& clip, float t);

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
    // clipDefs   : pModel's clip list (the controller stores raw pointers into it)
    // nodes      : the instance's scene node for each model node, by model node index
    // skeletons  : the instance's skeleton instance for each skeleton, by skeleton index
    //
    // Every track target must be in range; XModelBuilder::Build checks this.
    //--------------------------------------------------------------------------------------------
    void BuildFromModel(
        XModel* pModel,
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

    GEMMETHOD_(uint32_t, GetActiveClipIndex)() final { return m_ActiveClipIndex; }
    GEMMETHOD_(float,    GetTime)() final { return m_Time; }
    GEMMETHOD_(void,     SetTime)(float seconds) final { m_Time = seconds; }
    GEMMETHOD_(bool,     IsPlaying)() final { return m_IsPlaying; }
};

} // namespace Canvas
