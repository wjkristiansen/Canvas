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
        return reinterpret_cast<Canvas::GfxVertexBuffer*>(m_pPositionBuffer.Get());
    case Canvas::GfxVertexBufferType::Normal:
        return reinterpret_cast<Canvas::GfxVertexBuffer*>(m_pNormalBuffer.Get());
    default:
        return nullptr;
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(Canvas::XGfxMaterial*) CMeshData12::GetMaterial([[maybe_unused]] uint32_t materialIndex)
{
    // Not implemented for debug mesh
    return nullptr;
}

//------------------------------------------------------------------------------------------------
void CMeshData12::SetPositionBuffer(Canvas::XGfxBuffer* pBuffer)
{
    m_pPositionBuffer = pBuffer;
}

//------------------------------------------------------------------------------------------------
void CMeshData12::SetNormalBuffer(Canvas::XGfxBuffer* pBuffer)
{
    m_pNormalBuffer = pBuffer;
}
