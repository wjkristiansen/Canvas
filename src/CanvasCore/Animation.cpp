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

// Overwrite the first n components of out with the packed value at p.
static void LoadValue(const float* p, uint32_t n, Math::FloatVector4& out)
{
    for (uint32_t c = 0; c < n; ++c)
        out[c] = p[c];
}

// Evaluate a track at time t into the components of out that its value type has.
// Clamps to the first/last key outside the range; within range, interpolates between the
// bracketing keys as the track's interpolation says.
void CAnimationController::EvaluateTrack(const AnimTrack& track, float t, Math::FloatVector4& out)
{
    const std::vector<float>& times = track.Times;
    if (times.empty())
        return;

    const uint32_t n = AnimationValueComponents(track.ValueType);
    const float* values = track.Values.data();

    if (!(t >= times.front()))   // before the first key, or NaN
    {
        LoadValue(values, n, out);
        return;
    }
    if (t >= times.back())
    {
        LoadValue(values + (times.size() - 1) * n, n, out);
        return;
    }

    // Binary search for the first key with time > t, so at the time of two keys that share it the
    // later key applies.  The bracketing keys are at different times, since t is at or after the
    // earlier and before the later.
    const size_t next = static_cast<size_t>(std::upper_bound(times.begin(), times.end(), t) - times.begin());
    const size_t prev = next - 1;
    const float* v0 = values + prev * n;
    const float* v1 = values + next * n;
    const float u = (t - times[prev]) / (times[next] - times[prev]);

    switch (track.Interpolation)
    {
    case AnimationInterpolation::Step:
        LoadValue(v0, n, out);
        break;

    case AnimationInterpolation::Cubic:
    {
        const float* c = track.Coefficients.data() + prev * 3 * n;
        for (uint32_t i = 0; i < n; ++i)
            out[i] = v0[i] + u * (c[i] + u * (c[n + i] + u * c[2 * n + i]));
        break;
    }

    default:
        if (track.ValueType == AnimationValueType::Quaternion)
        {
            out = Slerp(Math::FloatQuaternion(v0[0], v0[1], v0[2], v0[3]),
                        Math::FloatQuaternion(v1[0], v1[1], v1[2], v1[3]), u);
        }
        else
        {
            for (uint32_t i = 0; i < n; ++i)
                out[i] = v0[i] + (v1[i] - v0[i]) * u;
        }
        break;
    }
}

Transform CAnimationController::BoundTransform::Get() const
{
    if (pSkeleton)
        return pSkeleton->LocalTransform(Bone);

    return pNode->GetLocalTransform();
}

void CAnimationController::BoundTransform::Set(const Transform &transform) const
{
    if (pSkeleton)
    {
        pSkeleton->SetLocalTransform(Bone, transform);
        return;
    }

    pNode->SetLocalTransform(transform);
}

// The value of one property of a transform.
static Math::FloatVector4 PropertyOf(const Transform& transform, TransformProperty property)
{
    switch (property)
    {
    case TransformProperty::Rotation: return transform.Rotation;
    case TransformProperty::Scale:    return transform.Scale;
    default:                          return transform.Translation;
    }
}

Math::FloatVector4 CAnimationController::ReadTarget(size_t i) const
{
    const BoundTarget& target = m_Targets[i];
    if (target.Transform == InvalidIndex)
        return m_Values[i];
    return PropertyOf(m_Transforms[target.Transform].Get(), target.Property);
}

void CAnimationController::EvaluateClipPose(const CAnimationClip& clip, float t)
{
    m_Pose = m_BindValues;
    for (size_t ti = 0; ti < clip.Tracks.size(); ++ti)
        EvaluateTrack(clip.Tracks[ti], t, m_Pose[clip.TrackTargets[ti]]);
}

void CAnimationController::ApplyValues(const std::vector<Math::FloatVector4>& values)
{
    // A transform keeps the properties no target writes.
    for (size_t i = 0; i < m_Transforms.size(); ++i)
        m_TransformScratch[i] = m_Transforms[i].Get();

    for (size_t i = 0; i < m_Targets.size(); ++i)
    {
        const Math::FloatVector4& v = values[i];
        m_Values[i] = v;

        const BoundTarget& target = m_Targets[i];
        if (target.Transform == InvalidIndex)
            continue;

        Transform& transform = m_TransformScratch[target.Transform];
        switch (target.Property)
        {
        case TransformProperty::Translation: transform.Translation = v; break;
        case TransformProperty::Rotation:    transform.Rotation    = v; break;
        case TransformProperty::Scale:       transform.Scale       = v; break;
        }
    }

    for (size_t i = 0; i < m_Transforms.size(); ++i)
        m_Transforms[i].Set(m_TransformScratch[i]);
}

void CAnimationController::ApplyClipPose(const CAnimationClip& clip, float t)
{
    EvaluateClipPose(clip, t);
    ApplyValues(m_Pose);
}

void CAnimationController::ApplyBlendedPose(float weight)
{
    if (!m_pClipDefs || m_ActiveClipIndex >= m_pClipDefs->size()) return;
    EvaluateClipPose((*m_pClipDefs)[m_ActiveClipIndex], m_Time);

    for (size_t i = 0; i < m_Targets.size(); ++i)
    {
        const Math::FloatVector4& from = m_BlendFromPose[i];
        Math::FloatVector4& to = m_Pose[i];
        if (m_Targets[i].ValueType == AnimationValueType::Quaternion)
            to = Slerp(from, to, weight);
        else
            to = from + (to - from) * weight;
    }
    ApplyValues(m_Pose);
}

//================================================================================================
// BuildFromModel
//================================================================================================

void CAnimationController::BuildFromModel(
    XModel* pModel,
    const std::vector<AnimTarget>& targets,
    const std::vector<CAnimationClip>& clipDefs,
    const std::vector<XSceneNode*>& nodes,
    const std::vector<CSkeletonInstance*>& skeletons)
{
    m_pModel    = pModel;       // keep the shared clip data alive for the controller's lifetime
    m_pClipDefs = &clipDefs;
    m_Transforms.clear();
    m_Targets.clear();
    m_BindValues.clear();
    m_BlendFromPose.clear();

    // One bound transform per transform any target writes, keyed by (skeleton + 1, index);
    // 0 in the high half of the key stands for model nodes.
    std::unordered_map<uint64_t, uint32_t> transformSlots;

    m_Targets.reserve(targets.size());
    m_BindValues.reserve(targets.size());
    for (const AnimTarget& def : targets)
    {
        BoundTarget target;
        target.ValueType = def.ValueType;

        if (def.Type == AnimationTargetType::Value)
        {
            m_Targets.push_back(target);
            m_BindValues.push_back(Math::FloatVector4(0.0f, 0.0f, 0.0f, 0.0f));
            continue;
        }

        const bool isBone = IsBoneTarget(def.Type);
        const uint64_t owner = isBone ? static_cast<uint64_t>(def.SkeletonIndex) + 1 : 0;
        const uint64_t key = (owner << 32) | def.Index;

        auto [it, inserted] = transformSlots.try_emplace(key, static_cast<uint32_t>(m_Transforms.size()));
        if (inserted)
        {
            BoundTransform transform;
            if (isBone)
            {
                transform.pSkeleton = skeletons[def.SkeletonIndex];
                transform.Bone      = def.Index;
            }
            else
            {
                transform.pNode = nodes[def.Index];
            }
            m_Transforms.push_back(transform);
        }

        target.Transform = it->second;
        target.Property  = TargetProperty(def.Type);
        m_Targets.push_back(target);
        m_BindValues.push_back(PropertyOf(m_Transforms[target.Transform].Get(), target.Property));
    }

    m_TransformScratch.resize(m_Transforms.size());
    m_Values = m_BindValues;
    m_Pose   = m_BindValues;

    m_ActiveClipIndex = 0;
    m_Time            = 0.0f;
    m_IsPlaying       = false;
    m_InBindPose      = true;
}

//================================================================================================
// XAnimationController interface methods
//================================================================================================

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
        m_BlendFromPose.resize(m_Targets.size());
        for (size_t i = 0; i < m_Targets.size(); ++i)
            m_BlendFromPose[i] = ReadTarget(i);
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
    ApplyValues(m_BindValues);
    m_IsPlaying     = false;
    m_InBindPose    = true;
    m_BlendFromPose.clear();
    m_BlendDuration = 0.0f;
    m_BlendTime     = 0.0f;
}

GEMMETHODIMP CAnimationController::GetTargetValue(uint32_t targetIndex, _Out_ float *pValue, uint32_t count)
{
    if (!pValue)
        return Gem::Result::BadPointer;
    if (targetIndex >= m_Values.size())
        return Gem::Result::OutOfRange;
    if (count != AnimationValueComponents(m_Targets[targetIndex].ValueType))
        return Gem::Result::InvalidArg;
    for (uint32_t c = 0; c < count; ++c)
        pValue[c] = m_Values[targetIndex][static_cast<int>(c)];
    return Gem::Result::Success;
}

//================================================================================================
// Update - called each frame by CSceneNode::UpdateObjectNodes
//================================================================================================

GEMMETHODIMP CAnimationController::Update(float dtime)
{
    if (!m_IsPlaying || !m_pClipDefs || m_ActiveClipIndex >= m_pClipDefs->size())
        return Gem::Result::Success;

    const CAnimationClip& clip = (*m_pClipDefs)[m_ActiveClipIndex];

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
            ApplyClipPose(clip, m_Time);
        }
        else
        {
            const float weight = m_BlendTime / m_BlendDuration;
            ApplyBlendedPose(weight);
        }
    }
    else
    {
        ApplyClipPose(clip, m_Time);
    }

    return Gem::Result::Success;
}

} // namespace Canvas
