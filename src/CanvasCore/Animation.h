//================================================================================================
// Animation — runtime clip storage and per-instance animation controller
//
// CAnimationClip   : immutable, shared clip data stored on CModel (one per FBX AnimationStack
//                    / Blender Action).  All instances of a model share the same clip data.
//
// CAnimationController : mutable per-instance evaluator, bound as a XSceneGraphElement to
//                        the instance root node so CSceneGraphNode::Update(dtime) drives it
//                        automatically each frame.
//
//                        Supports immediate clip switching and linear cross-fade blending.
//                        Bind-pose restoration snapshots the cloned node TRS at instantiation
//                        time and restores it on demand (useful for editing / debugging).
//================================================================================================

#pragma once

#include "pch.h"
#include "Canvas.h"
#include "CanvasElement.h"
#include "SceneGraph.h"

namespace Canvas
{

//================================================================================================
// CAnimationClip — immutable clip data (shared across instances)
//================================================================================================

struct AnimNodeTrack
{
    std::string                     NodeName;
    std::vector<AnimationKeyframe>  Keyframes;  // sorted ascending by Time
};

struct CAnimationClip
{
    std::string                 Name;
    float                       Duration = 0.0f;    // seconds
    std::vector<AnimNodeTrack>  Tracks;
};

//================================================================================================
// CAnimationController — per-instance evaluator
//================================================================================================

class CAnimationController :
    public TCanvasElement<XAnimationController>
{
    //--------------------------------------------------------------------------------------------
    // Per-track runtime binding: resolved cloned node + keyframe data pointer
    //--------------------------------------------------------------------------------------------
    struct BoundTrack
    {
        XSceneGraphNode*                      pNode = nullptr;
        const std::vector<AnimationKeyframe>* pKeys = nullptr;  // into m_Clips[i].Tracks[j].Keyframes
    };

    struct BoundClip
    {
        const CAnimationClip*       pDef = nullptr;
        std::vector<BoundTrack>     Tracks;
    };

    //--------------------------------------------------------------------------------------------
    // Bind-pose snapshot: captured at BuildFromModel time, restored by ResetToBindPose()
    //--------------------------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable: 4324)
    struct BindPoseEntry
    {
        XSceneGraphNode*                pNode = nullptr;    // non-owning; see note above
        Math::FloatVector4              T;
        Math::FloatQuaternion           R;
        Math::FloatVector4              S;
    };

    //--------------------------------------------------------------------------------------------
    // Per-node pose snapshot used during blending
    //--------------------------------------------------------------------------------------------
    struct PoseSnapshot
    {
        XSceneGraphNode*                pNode = nullptr;    // non-owning; see note above
        Math::FloatVector4              T;
        Math::FloatQuaternion           R;
        Math::FloatVector4              S;
    };
#pragma warning(pop)

    // Strong reference to the model that owns the shared clip data.  The controller stores
    // raw pointers into the model's clip vector (m_pClipDefs and the per-track keyframe
    // pointers below), so it must keep the model alive for its own lifetime; an instance is
    // otherwise free to outlive the XModel it was created from (as it does for mesh/material
    // data, which instances co-own via their own Gem references).
    Gem::TGemPtr<XModel>                m_pModel;

    // Shared clip definitions - pointer into m_pModel's vector (kept alive by m_pModel above)
    std::vector<CAnimationClip> const*  m_pClipDefs = nullptr;
    std::vector<BoundClip>              m_BoundClips;       // parallel to *m_pClipDefs
    std::vector<BindPoseEntry>          m_BindPose;         // one per uniquely animated node

    uint32_t    m_ActiveClipIndex   = 0;
    float       m_Time              = 0.0f;
    bool        m_IsPlaying         = false;
    bool        m_InBindPose        = true;

    // Blend state
    std::vector<PoseSnapshot>   m_BlendFromPose;    // empty when not blending
    float                       m_BlendTime         = 0.0f;
    float                       m_BlendDuration     = 0.0f;

    //--------------------------------------------------------------------------------------------
    // Helpers
    //--------------------------------------------------------------------------------------------
    static Math::FloatQuaternion Slerp(
        Math::FloatQuaternion a, Math::FloatQuaternion b, float t);

    static void EvaluateTrack(
        const std::vector<AnimationKeyframe>& keys, float t,
        Math::FloatVector4& outT, Math::FloatQuaternion& outR, Math::FloatVector4& outS);

    // Snapshot the current animated-node poses into dest (used before a blend transition)
    void SnapshotPose(std::vector<PoseSnapshot>& dest) const;

    // Apply the active clip at m_Time to all bound nodes (no blending)
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
    // pModel    : model that owns the clip data; held by a strong reference so the controller
    //             can keep the shared clips alive for its own lifetime
    // clipDefs  : pModel's clip list (the controller stores raw pointers into it)
    // cloneMap  : maps model-template node* -> cloned instance node* (from Instantiate)
    //--------------------------------------------------------------------------------------------
    void BuildFromModel(
        XModel* pModel,
        const std::vector<CAnimationClip>& clipDefs,
        const std::unordered_map<XSceneGraphNode*, Gem::TGemPtr<XSceneGraphNode>>& cloneMap);

    // XAnimationController
    GEMMETHOD(Update)(float dtime) final;

    // XAnimationController
    GEMMETHOD_(uint32_t, GetClipCount)() final
    {
        return m_pClipDefs ? static_cast<uint32_t>(m_pClipDefs->size()) : 0u;
    }

    GEMMETHOD_(PCSTR, GetClipName)(uint32_t index) final
    {
        if (!m_pClipDefs || index >= m_pClipDefs->size()) return nullptr;
        return (*m_pClipDefs)[index].Name.c_str();
    }

    GEMMETHOD_(bool, ValidateForNode)(_In_ XSceneGraphNode *pRoot) final;

    GEMMETHOD_(uint32_t, FindClip)(PCSTR name) final;

    GEMMETHOD(Play)(uint32_t clipIndex, float blendDuration) final;
    GEMMETHOD(PlayByName)(PCSTR clipName, float blendDuration) final;
    GEMMETHOD_(void, Stop)() final;
    GEMMETHOD_(void, ResetToBindPose)() final;

    GEMMETHOD_(uint32_t, GetActiveClipIndex)() final { return m_ActiveClipIndex; }
    GEMMETHOD_(float,    GetTime)() final { return m_Time; }
    GEMMETHOD_(void,     SetTime)(float seconds) final { m_Time = seconds; }
    GEMMETHOD_(bool,     IsPlaying)() final { return m_IsPlaying; }
};

} // namespace Canvas
