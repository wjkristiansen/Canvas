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
GEMMETHODIMP_(Canvas::GfxVertexBuffer*) CMeshData12::GetVertexBuffer(uint32_t materialIndex, Canvas::GfxVertexBufferType type)
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
void CMeshData12::SetPositionBuffer(Canvas::XGfxBuffer* pBuffer)
{
    m_PositionVB.pBuffer = pBuffer;
    m_PositionVB.Offset = 0;
}

//------------------------------------------------------------------------------------------------
void CMeshData12::SetNormalBuffer(Canvas::XGfxBuffer* pBuffer)
{
    m_NormalVB.pBuffer = pBuffer;
    m_NormalVB.Offset = 0;
}
