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

// Evaluate a track's TRS at time t.
// Clamps to the first/last keyframe outside the range; linearly interpolates between
// the bracketing keyframes within range.
void CAnimationController::EvaluateTrack(
    const std::vector<AnimationKeyframe>& keys, float t,
    Math::FloatVector4& outT, Math::FloatQuaternion& outR, Math::FloatVector4& outS)
{
    if (keys.empty())
        return;

    if (keys.size() == 1 || t <= keys.front().Time)
    {
        outT = keys.front().Translation;
        outR = keys.front().Rotation;
        outS = keys.front().Scale;
        return;
    }

    if (t >= keys.back().Time)
    {
        outT = keys.back().Translation;
        outR = keys.back().Rotation;
        outS = keys.back().Scale;
        return;
    }

    // Binary search for the first key with Time > t
    auto it = std::upper_bound(keys.begin(), keys.end(), t,
        [](float val, const AnimationKeyframe& k) { return val < k.Time; });

    const AnimationKeyframe& next = *it;
    const AnimationKeyframe& prev = *std::prev(it);

    const float span  = next.Time - prev.Time;
    const float alpha = (span > 1e-7f) ? (t - prev.Time) / span : 0.0f;

    outT = prev.Translation + (next.Translation - prev.Translation) * alpha;
    outR = Slerp(prev.Rotation, next.Rotation, alpha);
    outS = prev.Scale + (next.Scale - prev.Scale) * alpha;
}

void CAnimationController::SnapshotPose(std::vector<PoseSnapshot>& dest) const
{
    dest.clear();
    if (!m_pClipDefs) return;

    for (const BoundClip& bc : m_BoundClips)
    {
        for (const BoundTrack& bt : bc.Tracks)
        {
            if (!bt.pNode) continue;
            // Avoid duplicating the same node if it appears in multiple clips
            bool found = false;
            for (const PoseSnapshot& s : dest)
                if (s.pNode == bt.pNode) { found = true; break; }
            if (!found)
            {
                PoseSnapshot snap;
                snap.pNode = bt.pNode;
                snap.T = bt.pNode->GetLocalTranslation();
                snap.R = bt.pNode->GetLocalRotation();
                snap.S = bt.pNode->GetLocalScale();
                dest.push_back(std::move(snap));
            }
        }
    }
}

void CAnimationController::ApplyClipPose(const BoundClip& clip, float t)
{
    for (const BoundTrack& bt : clip.Tracks)
    {
        if (!bt.pNode || !bt.pKeys) continue;

        Math::FloatVector4    T = bt.pNode->GetLocalTranslation();
        Math::FloatQuaternion R = bt.pNode->GetLocalRotation();
        Math::FloatVector4    S = bt.pNode->GetLocalScale();

        EvaluateTrack(*bt.pKeys, t, T, R, S);

        bt.pNode->SetLocalTranslation(T);
        bt.pNode->SetLocalRotation(R);
        bt.pNode->SetLocalScale(S);
    }
}

void CAnimationController::ApplyBlendedPose(float weight)
{
    if (!m_pClipDefs || m_ActiveClipIndex >= m_pClipDefs->size()) return;
    const BoundClip& toClip = m_BoundClips[m_ActiveClipIndex];

    for (const BoundTrack& bt : toClip.Tracks)
    {
        if (!bt.pNode || !bt.pKeys) continue;

        // Find matching from-pose snapshot
        Math::FloatVector4    fromT = bt.pNode->GetLocalTranslation();
        Math::FloatQuaternion fromR = bt.pNode->GetLocalRotation();
        Math::FloatVector4    fromS = bt.pNode->GetLocalScale();

        for (const PoseSnapshot& snap : m_BlendFromPose)
        {
            if (snap.pNode == bt.pNode)
            {
                fromT = snap.T;
                fromR = snap.R;
                fromS = snap.S;
                break;
            }
        }

        Math::FloatVector4    toT = fromT;
        Math::FloatQuaternion toR = fromR;
        Math::FloatVector4    toS = fromS;
        EvaluateTrack(*bt.pKeys, m_Time, toT, toR, toS);

        bt.pNode->SetLocalTranslation(fromT + (toT - fromT) * weight);
        bt.pNode->SetLocalRotation(Slerp(fromR, toR, weight));
        bt.pNode->SetLocalScale(fromS + (toS - fromS) * weight);
    }
}

//================================================================================================
// BuildFromModel
//================================================================================================

void CAnimationController::BuildFromModel(
    XModel* pModel,
    const std::vector<CAnimationClip>& clipDefs,
    const std::unordered_map<XSceneGraphNode*, Gem::TGemPtr<XSceneGraphNode>>& cloneMap)
{
    m_pModel    = pModel;       // keep the shared clip data alive for the controller's lifetime
    m_pClipDefs = &clipDefs;
    m_BoundClips.clear();
    m_BindPose.clear();

    // Build a name->cloned-node lookup from cloneMap
    std::unordered_map<std::string, XSceneGraphNode*> nodeByName;
    for (const auto& kv : cloneMap)
    {
        if (kv.first && kv.second)
        {
            PCSTR name = kv.first->GetName();
            if (name && name[0])
                nodeByName[name] = kv.second.Get();
        }
    }

    // Bind pose snapshot - collect all uniquely animated nodes first
    std::unordered_map<XSceneGraphNode*, bool> seenForBindPose;

    for (const CAnimationClip& def : clipDefs)
    {
        BoundClip bc;
        bc.pDef = &def;

        for (const AnimNodeTrack& track : def.Tracks)
        {
            auto it = nodeByName.find(track.NodeName);
            if (it == nodeByName.end() || !it->second) continue;

            XSceneGraphNode* pNode = it->second;

            BoundTrack bt;
            bt.pNode = pNode;
            bt.pKeys = &track.Keyframes;
            bc.Tracks.push_back(std::move(bt));

            // Record bind pose once per node
            if (seenForBindPose.find(pNode) == seenForBindPose.end())
            {
                seenForBindPose[pNode] = true;
                BindPoseEntry entry;
                entry.pNode = pNode;
                entry.T = pNode->GetLocalTranslation();
                entry.R = pNode->GetLocalRotation();
                entry.S = pNode->GetLocalScale();
                m_BindPose.push_back(std::move(entry));
            }
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

GEMMETHODIMP_(bool) CAnimationController::ValidateForNode(XSceneGraphNode *pRoot)
{
    if (!pRoot) return false;

    // Walk every bound node across all clips; each must be a descendant of pRoot (or pRoot itself).
    auto IsDescendant = [](XSceneGraphNode* pCandidate, XSceneGraphNode* pAncestor) -> bool
    {
        for (XSceneGraphNode* p = pCandidate; p; p = p->GetParent())
            if (p == pAncestor) return true;
        return false;
    };

    for (const BoundClip& bc : m_BoundClips)
    {
        for (const BoundTrack& bt : bc.Tracks)
        {
            if (bt.pNode && !IsDescendant(bt.pNode, pRoot))
                return false;
        }
    }
    return true;
}

GEMMETHODIMP_(uint32_t) CAnimationController::FindClip(PCSTR name)
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

GEMMETHODIMP CAnimationController::PlayByName(PCSTR clipName, float blendDuration)
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
    for (const BindPoseEntry& entry : m_BindPose)
    {
        if (!entry.pNode) continue;
        entry.pNode->SetLocalTranslation(entry.T);
        entry.pNode->SetLocalRotation(entry.R);
        entry.pNode->SetLocalScale(entry.S);
    }
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

