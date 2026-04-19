//================================================================================================
// MeshData12
//================================================================================================

#include "pch.h"
#include "MeshData12.h"

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

