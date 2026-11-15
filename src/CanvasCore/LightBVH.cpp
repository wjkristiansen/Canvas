//================================================================================================
// LightBVH implementation.
//================================================================================================

#include "pch.h"
#include "LightBVH.h"
#include "Canvas.h"

namespace Canvas
{

namespace
{

// Exact AABB of the spot light's influence region: the spherical
// sector of radius `range` around `apex`, containing every direction
// within `outerHalfAngleRadians` of the unit `axis`.  This matches the
// shader, which lights a point when its distance is within the cutoff
// (never more than `range`) and its angle from the axis is inside the
// outer cone.
//
// The extreme of the sector along each world direction +/-e is one of:
//   - the apex;
//   - the sphere point apex + range * e, if e lies inside the cone;
//   - the extreme of the rim circle (center apex + axis * range*cos,
//     radius range*sin, normal axis).  Along world axis i a circle of
//     radius r with unit normal n extends r * sqrt(1 - n_i^2) from its
//     center.
// Valid for any half-angle in [0, pi], including cones wider than a
// hemisphere.
Math::AABB ComputeSpotInfluenceAABB(const Math::FloatVector4& apex,
                                    const Math::FloatVector4& axis,
                                    float range,
                                    float outerHalfAngleRadians)
{
    Math::AABB box;
    box.ExpandToInclude(apex);

    if (range <= 0.0f)
        return box;

    const float halfAngle = std::clamp(outerHalfAngleRadians, 0.0f, 3.14159265f);
    const float cosHalf = std::cos(halfAngle);
    const float rimRadius = range * std::sin(halfAngle);
    const float rimOffset = range * cosHalf;

    Math::FloatVector4 lo(apex.V[0], apex.V[1], apex.V[2], 0.0f);
    Math::FloatVector4 hi = lo;
    for (int i = 0; i < 3; ++i)
    {
        const float a = axis.V[i];
        const float rimCenter = apex.V[i] + a * rimOffset;
        const float rimExtent = rimRadius * std::sqrt((std::max)(0.0f, 1.0f - a * a));
        lo.V[i] = (std::min)(lo.V[i], rimCenter - rimExtent);
        hi.V[i] = (std::max)(hi.V[i], rimCenter + rimExtent);

        // dot(+e_i, axis) = a; dot(-e_i, axis) = -a.
        if (a >= cosHalf)
            hi.V[i] = apex.V[i] + range;
        if (-a >= cosHalf)
            lo.V[i] = apex.V[i] - range;
    }
    box.ExpandToInclude(lo);
    box.ExpandToInclude(hi);
    return box;
}

// AABB of the point-light influence sphere: simply the bbox of the
// sphere of radius `range` centered at `position`.
Math::AABB ComputePointInfluenceAABB(const Math::FloatVector4& position, float range)
{
    if (range <= 0.0f)
        return Math::AABB{};
    return Math::AABB(
        Math::FloatVector4(position.V[0] - range, position.V[1] - range, position.V[2] - range, 0.0f),
        Math::FloatVector4(position.V[0] + range, position.V[1] + range, position.V[2] + range, 0.0f));
}

// Derive (worldPosition, axisForward) from a light's attached node's
// global matrix.  Position is row 3; the spot/directional "forward"
// is row 0, the local forward axis.  Returns false when no node is attached.
bool GetLightWorldPose(XLight* light,
                       Math::FloatVector4& outPosition,
                       Math::FloatVector4& outForward)
{
    XSceneNode* node = light->GetAttachedNode();
    if (!node)
        return false;
    const Math::FloatMatrix4x4 world = node->GetGlobalMatrix();
    outPosition = Math::FloatVector4(world[3][0], world[3][1], world[3][2], 1.0f);
    outForward  = Math::FloatVector4(world[0][0], world[0][1], world[0][2], 0.0f);
    return true;
}

} // anonymous namespace

void LightBVH::Clear()
{
    m_Primitives.clear();
    m_LightForPrimitive.clear();
    m_UntrackedLights.clear();
    m_TrackedLightSet.clear();
    m_BVH = BVH{};
    m_VisiblePrimitiveScratch.clear();
    m_Built = false;
}

void LightBVH::Build(XSceneNode* root)
{
    Clear();

    if (!root)
        return;

    // Iterative DFS to match WorldBVH (and to defend against the
    // deep instanced subtrees that can occur in imported scenes).
    std::vector<XSceneNode*> stack;
    stack.reserve(64);
    stack.push_back(root);

    BVHPrimitiveId nextId = 0;
    while (!stack.empty())
    {
        XSceneNode* node = stack.back();
        stack.pop_back();
        if (!node)
            continue;

        const uint32_t elementCount = node->GetBoundElementCount();
        for (uint32_t i = 0; i < elementCount; ++i)
        {
            XSceneElement* element = node->GetBoundElement(i);
            if (!element)
                continue;

            Gem::TGemPtr<XLight> pLight;
            if (FAILED(element->QueryInterface(&pLight)))
                continue;

            XLight* lightRaw = pLight.Get();
            const LightType type = lightRaw->GetType();

            // Directional / Ambient / Area / disabled-attached light:
            // no spatial bound, route to the untracked list so the
            // consumer can include it unconditionally.
            if (type != LightType::Point && type != LightType::Spot)
            {
                m_UntrackedLights.push_back(lightRaw);
                continue;
            }

            Math::FloatVector4 worldPos, forward;
            if (!GetLightWorldPose(lightRaw, worldPos, forward))
            {
                // A point/spot light with no attached node has no
                // meaningful world position; treat as untracked so it
                // still reaches the renderer (which will handle the
                // missing-node fallback) rather than silently
                // dropping it.
                m_UntrackedLights.push_back(lightRaw);
                continue;
            }

            const float range = lightRaw->GetRange();
            Math::AABB influence;

            if (type == LightType::Point)
            {
                influence = ComputePointInfluenceAABB(worldPos, range);
            }
            else // LightType::Spot
            {
                float innerHalf = 0.785398f;
                float outerHalf = 1.047198f;
                lightRaw->GetSpotAngles(&innerHalf, &outerHalf);
                // SetSpotAngles takes the full angle pair; the cone
                // half-angle for influence bounding is outer / 2.
                // (Mirrors the cosine used by the renderer's spot
                // attenuation.)
                const float outerHalfAngle = 0.5f * outerHalf;

                // Defensive normalize: imported / animated transforms
                // can drift basis rows slightly off unit length.
                const float flen = std::sqrt(forward.V[0] * forward.V[0]
                                           + forward.V[1] * forward.V[1]
                                           + forward.V[2] * forward.V[2]);
                if (flen > 1e-6f)
                {
                    forward.V[0] /= flen;
                    forward.V[1] /= flen;
                    forward.V[2] /= flen;
                }
                influence = ComputeSpotInfluenceAABB(worldPos, forward, range, outerHalfAngle);
            }

            if (influence.IsEmpty())
            {
                // Zero-range or otherwise degenerate; classify as
                // untracked so the renderer still sees it (it will
                // produce zero contribution at runtime anyway, but
                // we shouldn't silently drop authored lights).
                m_UntrackedLights.push_back(lightRaw);
                continue;
            }

            BVHPrimitive prim;
            prim.WorldBounds = influence;
            prim.Node        = node;
            prim.Id          = nextId++;
            prim.Flags       = BVHPrimitiveFlags::Static;
            m_Primitives.push_back(prim);
            m_LightForPrimitive.push_back(lightRaw);
            m_TrackedLightSet.insert(lightRaw);
        }

        for (XSceneNode* child = node->GetFirstChild();
             child;
             child = node->GetNextChild(child))
        {
            stack.push_back(child);
        }
    }

    if (!m_Primitives.empty())
        m_BVH.Build(m_Primitives.data(), m_Primitives.size());

    m_Built = true;
}

bool LightBVH::IsTrackedLight(XLight* light) const
{
    if (!light)
        return false;
    return m_TrackedLightSet.find(light) != m_TrackedLightSet.end();
}

void LightBVH::QueryFrustum(XCamera* camera, std::vector<XLight*>& outVisibleLights) const
{
    outVisibleLights.clear();
    if (!camera || !m_Built || m_Primitives.empty())
        return;

    const Math::FloatMatrix4x4 vp = camera->GetViewProjectionMatrix();
    const Math::Frustum f = Math::Frustum::FromViewProjection(vp, /*reverseZ*/ true);
    QueryFrustum(f, outVisibleLights);
}

void LightBVH::QueryFrustum(const Math::Frustum& frustum,
                            std::vector<XLight*>& outVisibleLights) const
{
    outVisibleLights.clear();
    if (!m_Built || m_Primitives.empty())
        return;

    m_VisiblePrimitiveScratch.clear();
    m_BVH.QueryFrustum(m_Primitives.data(), frustum, m_VisiblePrimitiveScratch);

    // Each tracked light contributes exactly one primitive, so a
    // primitive-index -> light mapping is sufficient and no dedup
    // pass is required here.
    outVisibleLights.reserve(m_VisiblePrimitiveScratch.size());
    for (uint32_t primIdx : m_VisiblePrimitiveScratch)
        outVisibleLights.push_back(m_LightForPrimitive[primIdx]);
}

} // namespace Canvas
