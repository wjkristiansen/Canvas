//================================================================================================
// LightBVH implementation.
//================================================================================================

#include "pch.h"
#include "LightBVH.h"
#include "Canvas.h"
#include "SceneGraph.h"

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

    const float halfAngle = std::clamp(outerHalfAngleRadians, 0.0f, Math::PiF);
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

// The light's influence AABB with `world` as its node's global matrix,
// or the empty AABB when the light has no spatial bound: directional,
// ambient and area lights, and point lights with zero range.
Math::AABB ComputeInfluence(XLight* light, const Math::FloatMatrix4x4& world)
{
    const LightType type = light->GetType();
    if (type != LightType::Point && type != LightType::Spot)
        return Math::AABB{};

    // Position is row 3; the spot "forward" is row 0, the local
    // forward axis.
    const Math::FloatVector4 worldPos = world[3];
    const Math::FloatVector4 forward = world[0];
    const float range = light->GetRange();

    if (type == LightType::Point)
        return ComputePointInfluenceAABB(worldPos, range);

    // Full cone angles.  The renderer lights out to the larger of the
    // two, swapping them when inner exceeds outer, and its cone
    // half-angle is half of that.  GetSpotAngles always writes both.
    float innerAngle = 0.0f;
    float outerAngle = 0.0f;
    light->GetSpotAngles(&innerAngle, &outerAngle);
    const float outerHalfAngle = 0.5f * (std::max)(innerAngle, outerAngle);

    // Scale anywhere above the light's node leaves the basis row off
    // unit length.  Normalize as the renderer does, so a degenerate
    // axis falls back to the same direction it lights.
    return ComputeSpotInfluenceAABB(worldPos, forward.Normalize(), range, outerHalfAngle);
}

} // anonymous namespace

void LightBVH::Clear()
{
    for (const LitNode& lit : m_LitNodeList)
        m_Tracker.Untrack(lit.Node);

    m_pRoot = nullptr;
    m_LitNodeList.clear();
    m_Lights.clear();
    m_LitNodes.clear();
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

    m_pRoot = root;

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

        const uint32_t firstLight = static_cast<uint32_t>(m_Lights.size());
        const uint32_t elementCount = node->GetBoundElementCount();
        Math::FloatMatrix4x4 world;
        for (uint32_t i = 0; i < elementCount; ++i)
        {
            Gem::TGemPtr<XLight> pLight;
            if (FAILED(node->GetBoundElement(i)->QueryInterface(&pLight)))
                continue;

            if (m_Lights.size() == firstLight)
                world = node->GetGlobalMatrix();

            // Directional / Ambient / Area lights, and zero-range point
            // lights, have no spatial bound: route them to the
            // untracked list so the consumer includes them
            // unconditionally rather than silently dropping authored
            // lights.
            const Math::AABB influence = ComputeInfluence(pLight, world);
            if (influence.IsEmpty())
            {
                m_UntrackedLights.push_back(pLight.Get());
                m_Lights.push_back({ pLight, kInvalidBVHPrimitiveId });
                continue;
            }

            BVHPrimitive prim;
            prim.WorldBounds = influence;
            prim.Node        = node;
            prim.Id          = nextId++;
            prim.Flags       = BVHPrimitiveFlags::Static;
            m_Primitives.push_back(prim);
            m_LightForPrimitive.push_back(pLight.Get());
            m_TrackedLightSet.insert(pLight.Get());
            m_Lights.push_back({ pLight, prim.Id });
        }

        const uint32_t lightCount = static_cast<uint32_t>(m_Lights.size()) - firstLight;
        if (lightCount > 0)
        {
            m_LitNodes.emplace(node, static_cast<uint32_t>(m_LitNodeList.size()));
            m_LitNodeList.push_back({ node, firstLight, lightCount });
            m_Tracker.Track(node);
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

void LightBVH::Refit()
{
    if (!m_Built)
        return;

    bool changed = false;
    bool rebuild = false;
    for (XSceneNode* node : m_Tracker.GetChangedNodes())
    {
        auto it = m_LitNodes.find(node);
        if (it == m_LitNodes.end())
            continue; // had no lights at Build; reported for the WorldBVH

        const LitNode& lit = m_LitNodeList[it->second];
        const Math::FloatMatrix4x4 world = node->GetGlobalMatrix();
        for (uint32_t i = 0; i < lit.LightCount; ++i)
        {
            const LightEntry& entry = m_Lights[lit.FirstLight + i];
            const Math::AABB influence = ComputeInfluence(entry.Light, world);

            // An untracked light that gained a spatial bound, or a
            // tracked one that lost it, belongs in the other list;
            // only a Build re-sorts them.
            const bool tracked = entry.PrimitiveIndex != kInvalidBVHPrimitiveId;
            if (tracked == influence.IsEmpty())
            {
                rebuild = true;
                break;
            }
            if (tracked)
            {
                m_Primitives[entry.PrimitiveIndex].WorldBounds = influence;
                changed = true;
            }
        }
        if (rebuild)
            break;
    }

    if (rebuild)
    {
        // Build releases the held root; keep it alive across the call.
        Gem::TGemPtr<XSceneNode> pRoot = m_pRoot;
        Build(pRoot);
    }
    else if (changed)
    {
        m_BVH.Refit(m_Primitives.data(), m_Primitives.size());
    }
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
