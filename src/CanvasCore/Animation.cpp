//================================================================================================
// Animation
//================================================================================================

#include "pch.h"
#include "Animation.h"

#include <algorithm>
#include <cmath>

namespace Canvas
{

//================================================================================================
// CAnimationController - helpers
//================================================================================================

// Spherical-linear interpolation between two unit quaternions.
// Handles the hemisphere flip (dot < 0) and falls back to normalised lerp
// when the quaternions are nearly identical to avoid division by ~zero.
Math::FloatQuaternion CAnimationController::Slerp(
    Math::FloatQuaternion a, Math::FloatQuaternion b, float t)
{
    float dot = Math::DotProduct(a, b);

    // Keep the shortest arc
    if (dot < 0.0f)
    {
        b   = -b;
        dot = -dot;
    }

    // Fallback to normalised lerp when nearly parallel
    if (dot > 0.9995f)
        return Math::FloatQuaternion(a + (b - a) * t).Normalize();

    const float theta0    = acosf(dot);
    const float sinTheta0 = sinf(theta0);
    const float s0        = sinf((1.0f - t) * theta0) / sinTheta0;
    const float s1        = sinf(t * theta0) / sinTheta0;

    return Math::FloatQuaternion(a * s0 + b * s1);
}

// Evaluate a track's SRT at time t.
// Clamps to the first/last keyframe outside the range; linearly interpolates between
// the bracketing keyframes within range.
void CAnimationController::EvaluateTrack(
    const std::vector<AnimationKeyframe>& keys, float t, Transform& out)
{
    if (keys.empty())
        return;

    if (keys.size() == 1 || t <= keys.front().Time)
    {
        out.Translation = keys.front().Translation;
        out.Rotation    = keys.front().Rotation;
        out.Scale       = keys.front().Scale;
        return;
    }

    if (t >= keys.back().Time)
    {
        out.Translation = keys.back().Translation;
        out.Rotation    = keys.back().Rotation;
        out.Scale       = keys.back().Scale;
        return;
    }

    // Binary search for the first key with Time > t
    auto it = std::upper_bound(keys.begin(), keys.end(), t,
        [](float val, const AnimationKeyframe& k) { return val < k.Time; });

    const AnimationKeyframe& next = *it;
    const AnimationKeyframe& prev = *std::prev(it);

    const float span  = next.Time - prev.Time;
    const float alpha = (span > 1e-7f) ? (t - prev.Time) / span : 0.0f;

    out.Translation = prev.Translation + (next.Translation - prev.Translation) * alpha;
    out.Rotation    = Slerp(prev.Rotation, next.Rotation, alpha);
    out.Scale       = prev.Scale + (next.Scale - prev.Scale) * alpha;
}

Transform CAnimationController::TransformTarget::Get() const
{
    if (pSkeleton)
        return pSkeleton->LocalTransform(Joint);

    Transform t;
    t.Translation = pNode->GetLocalTranslation();
    t.Rotation    = pNode->GetLocalRotation();
    t.Scale       = pNode->GetLocalScale();
    return t;
}

void CAnimationController::TransformTarget::Set(const Transform &transform) const
{
    if (pSkeleton)
    {
        pSkeleton->SetLocalTransform(Joint, transform);
        return;
    }

    pNode->SetLocalTranslation(transform.Translation);
    pNode->SetLocalRotation(transform.Rotation);
    pNode->SetLocalScale(transform.Scale);
}

void CAnimationController::SnapshotPose(std::vector<Transform>& dest) const
{
    dest.clear();
    dest.reserve(m_Targets.size());
    for (const TransformTarget& target : m_Targets)
        dest.push_back(target.Get());
}

void CAnimationController::ApplyClipPose(const BoundClip& clip, float t)
{
    for (const BoundTrack& bt : clip.Tracks)
    {
        const TransformTarget& target = m_Targets[bt.TargetIndex];
        Transform pose = target.Get();
        EvaluateTrack(*bt.pKeys, t, pose);
        target.Set(pose);
    }
}

void CAnimationController::ApplyBlendedPose(float weight)
{
    if (!m_pClipDefs || m_ActiveClipIndex >= m_pClipDefs->size()) return;
    const BoundClip& toClip = m_BoundClips[m_ActiveClipIndex];

    for (const BoundTrack& bt : toClip.Tracks)
    {
        const TransformTarget& target = m_Targets[bt.TargetIndex];
        const Transform& from = m_BlendFromPose[bt.TargetIndex];

        Transform to = from;
        EvaluateTrack(*bt.pKeys, m_Time, to);

        Transform blended;
        blended.Translation = from.Translation + (to.Translation - from.Translation) * weight;
        blended.Rotation    = Slerp(from.Rotation, to.Rotation, weight);
        blended.Scale       = from.Scale + (to.Scale - from.Scale) * weight;
        target.Set(blended);
    }
}

//================================================================================================
// BuildFromModel
//================================================================================================

void CAnimationController::BuildFromModel(
    XModel* pModel,
    const std::vector<CAnimationClip>& clipDefs,
    const std::vector<XSceneGraphNode*>& nodes,
    const std::vector<CSkeletonInstance*>& skeletons)
{
    m_pModel    = pModel;       // keep the shared clip data alive for the controller's lifetime
    m_pClipDefs = &clipDefs;
    m_BoundClips.clear();
    m_Targets.clear();
    m_BindPose.clear();
    m_BlendFromPose.clear();

    // One target slot per uniquely animated transform, keyed by (skeleton + 1, index);
    // 0 in the high half of the key stands for model nodes.
    std::unordered_map<uint64_t, uint32_t> targetSlots;

    for (const CAnimationClip& def : clipDefs)
    {
        BoundClip bc;
        bc.pDef = &def;
        bc.Tracks.reserve(def.Tracks.size());

        for (const AnimTrack& track : def.Tracks)
        {
            const bool isJoint = track.TargetType == AnimationTargetType::Joint;
            const uint64_t owner = isJoint ? static_cast<uint64_t>(track.SkeletonIndex) + 1 : 0;
            const uint64_t key = (owner << 32) | track.TargetIndex;

            auto [it, inserted] = targetSlots.try_emplace(key, static_cast<uint32_t>(m_Targets.size()));
            if (inserted)
            {
                TransformTarget target;
                if (isJoint)
                {
                    target.pSkeleton = skeletons[track.SkeletonIndex];
                    target.Joint     = track.TargetIndex;
                }
                else
                {
                    target.pNode = nodes[track.TargetIndex];
                }
                m_Targets.push_back(target);
                m_BindPose.push_back(target.Get());
            }

            BoundTrack bt;
            bt.TargetIndex = it->second;
            bt.pKeys       = &track.Keyframes;
            bc.Tracks.push_back(bt);
        }

        m_BoundClips.push_back(std::move(bc));
    }

    m_ActiveClipIndex = 0;
    m_Time            = 0.0f;
    m_IsPlaying       = false;
    m_InBindPose      = true;
}

//================================================================================================
// XAnimationController interface methods
//================================================================================================

GEMMETHODIMP_(bool) CAnimationController::ValidateForNode(_In_ XSceneGraphNode *pRoot)
{
    if (!pRoot) return false;

    // Every target must be in pRoot's subtree (pRoot included): a transform target's node,
    // or for a joint, its skeleton instance's node.
    auto IsDescendant = [](XSceneGraphNode* pCandidate, XSceneGraphNode* pAncestor) -> bool
    {
        for (XSceneGraphNode* p = pCandidate; p; p = p->GetParent())
            if (p == pAncestor) return true;
        return false;
    };

    for (const TransformTarget& target : m_Targets)
    {
        XSceneGraphNode* pNode = target.pSkeleton ? target.pSkeleton->GetAttachedNode() : target.pNode;
        if (!pNode || !IsDescendant(pNode, pRoot))
            return false;
    }
    return true;
}

GEMMETHODIMP_(uint32_t) CAnimationController::FindClip(const char * name)
{
    if (!m_pClipDefs || !name) return InvalidClipIndex;
    for (size_t i = 0; i < m_pClipDefs->size(); ++i)
        if ((*m_pClipDefs)[i].Name == name)
            return static_cast<uint32_t>(i);
    return InvalidClipIndex;
}

GEMMETHODIMP CAnimationController::Play(uint32_t clipIndex, float blendDuration)
{
    if (!m_pClipDefs || clipIndex >= m_pClipDefs->size())
        return Gem::Result::InvalidArg;

    if (blendDuration > 0.0f)
    {
        SnapshotPose(m_BlendFromPose);
        m_BlendTime     = 0.0f;
        m_BlendDuration = blendDuration;
    }
    else
    {
        m_BlendFromPose.clear();
        m_BlendDuration = 0.0f;
        m_BlendTime     = 0.0f;
    }

    m_ActiveClipIndex = clipIndex;
    m_Time            = 0.0f;
    m_IsPlaying       = true;
    m_InBindPose      = false;
    return Gem::Result::Success;
}

GEMMETHODIMP CAnimationController::PlayByName(const char * clipName, float blendDuration)
{
    const uint32_t idx = FindClip(clipName);
    if (idx == InvalidClipIndex)
        return Gem::Result::InvalidArg;
    return Play(idx, blendDuration);
}

GEMMETHODIMP_(void) CAnimationController::Stop()
{
    m_IsPlaying     = false;
    m_BlendFromPose.clear();
    m_BlendDuration = 0.0f;
    m_BlendTime     = 0.0f;
}

GEMMETHODIMP_(void) CAnimationController::ResetToBindPose()
{
    for (size_t i = 0; i < m_Targets.size(); ++i)
        m_Targets[i].Set(m_BindPose[i]);
    m_IsPlaying     = false;
    m_InBindPose    = true;
    m_BlendFromPose.clear();
    m_BlendDuration = 0.0f;
    m_BlendTime     = 0.0f;
}

//================================================================================================
// Update - called each frame by CSceneGraphNode::Update
//================================================================================================

GEMMETHODIMP CAnimationController::Update(float dtime)
{
    if (!m_IsPlaying || !m_pClipDefs || m_ActiveClipIndex >= m_pClipDefs->size())
        return Gem::Result::Success;

    const CAnimationClip& clip = (*m_pClipDefs)[m_ActiveClipIndex];
    const BoundClip& bound     = m_BoundClips[m_ActiveClipIndex];

    if (clip.Duration <= 0.0f)
        return Gem::Result::Success;

    m_Time += dtime;

    // Loop continuously
    while (m_Time >= clip.Duration)
        m_Time -= clip.Duration;

    if (m_BlendDuration > 0.0f && !m_BlendFromPose.empty())
    {
        m_BlendTime += dtime;
        if (m_BlendTime >= m_BlendDuration)
        {
            // Blend complete
            m_BlendFromPose.clear();
            m_BlendDuration = 0.0f;
            m_BlendTime     = 0.0f;
            ApplyClipPose(bound, m_Time);
        }
        else
        {
            const float weight = m_BlendTime / m_BlendDuration;
            ApplyBlendedPose(weight);
        }
    }
    else
    {
        ApplyClipPose(bound, m_Time);
    }

    return Gem::Result::Success;
}

} // namespace Canvas
