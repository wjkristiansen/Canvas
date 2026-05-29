//================================================================================================
// MeshData12
//================================================================================================

#include "pch.h"
#include "MeshData12.h"

#include <algorithm>

//------------------------------------------------------------------------------------------------
CMeshData12::CMeshData12(Canvas::XCanvas* pCanvas, PCSTR name) :
    TGfxElement(pCanvas)
{
    if (name != nullptr)
        SetName(name);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(uint32_t) CMeshData12::GetNumMaterialGroups()
{
    return static_cast<uint32_t>(m_Groups.size());
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(Canvas::GfxResourceAllocation*) CMeshData12::GetVertexBuffer(uint32_t materialIndex, Canvas::GfxVertexBufferType type)
{
    if (materialIndex >= m_Groups.size())
        return nullptr;

    GroupResources &group = m_Groups[materialIndex];
    auto pickIfPresent = [](Canvas::GfxResourceAllocation &alloc) -> Canvas::GfxResourceAllocation *
    {
        return alloc.pBuffer ? &alloc : nullptr;
    };

    switch (type)
    {
    case Canvas::GfxVertexBufferType::Position: return pickIfPresent(group.PositionVB);
    case Canvas::GfxVertexBufferType::Normal:   return pickIfPresent(group.NormalVB);
    case Canvas::GfxVertexBufferType::UV0:      return pickIfPresent(group.UV0VB);
    case Canvas::GfxVertexBufferType::Tangent:  return pickIfPresent(group.TangentVB);
    default:                                    return nullptr;
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(Canvas::XGfxMaterial*) CMeshData12::GetMaterial(uint32_t materialIndex)
{
    if (materialIndex >= m_Groups.size())
        return nullptr;

    return m_Groups[materialIndex].pMaterial.Get();
}

//------------------------------------------------------------------------------------------------
// Inflate the bounds by the union of each group's material
// displacement.  Displaced meshes describe their pre-displacement
// CP envelope in m_LocalBounds; the engine grows the AABB outward
// in all three axes by the worst-case displacement magnitude so
// cull / shadow callers see the displaced geometry envelope.
//
// Each CP is offset by sample * baseN where |sample| <= mag, with
// mag = max(|MapBias|, |MapBias + MapScale|).  The CPU-side does
// not know which directions the per-CP normals point, so the
// inflation must be symmetric in every axis (the displacement
// vector for any CP has length at most mag in some direction).
// For the flat heightfield case (normals = +Z) this is looser
// than a Z-only inflation in X / Y, by `mag` per side -- typically
// a few percent of the tile dimensions and not a culling problem.
GEMMETHODIMP_(Canvas::Math::AABB) CMeshData12::GetLocalBounds()
{
    Canvas::Math::AABB bounds = m_LocalBounds;
    if (bounds.IsEmpty())
        return bounds;

    float mag = 0.0f;
    for (const auto& g : m_Groups)
    {
        const Canvas::GfxDisplacementDesc *pDisp =
            g.pMaterial ? g.pMaterial->GetDisplacement() : nullptr;
        if (!pDisp)
            continue;
        const float lo = pDisp->MapBias;
        const float hi = pDisp->MapBias + pDisp->MapScale;
        mag = std::max(mag, std::max(std::abs(lo), std::abs(hi)));
    }
    if (mag > 0.0f)
    {
        for (int i = 0; i < 3; ++i)
        {
            bounds.Min[i] -= mag;
            bounds.Max[i] += mag;
        }
    }
    return bounds;
}

