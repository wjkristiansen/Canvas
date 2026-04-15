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
    return 1;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(Canvas::GfxResourceAllocation*) CMeshData12::GetVertexBuffer(uint32_t materialIndex, Canvas::GfxVertexBufferType type)
{
    if (materialIndex != 0)
        return nullptr;

    switch (type)
    {
    case Canvas::GfxVertexBufferType::Position:
        return m_PositionVB.pBuffer ? &m_PositionVB : nullptr;
    case Canvas::GfxVertexBufferType::Normal:
        return m_NormalVB.pBuffer ? &m_NormalVB : nullptr;
    default:
        return nullptr;
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(Canvas::XGfxMaterial*) CMeshData12::GetMaterial([[maybe_unused]] uint32_t materialIndex)
{
    // Material binding is not implemented yet for mesh data.
    return nullptr;
}

//------------------------------------------------------------------------------------------------
void CMeshData12::SetPositionBuffer(const Canvas::GfxResourceAllocation& vb)
{
    m_PositionVB = vb;
}

//------------------------------------------------------------------------------------------------
void CMeshData12::SetNormalBuffer(const Canvas::GfxResourceAllocation& vb)
{
    m_NormalVB = vb;
}
