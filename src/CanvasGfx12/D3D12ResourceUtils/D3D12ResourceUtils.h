//================================================================================================
// Resource
//================================================================================================

#pragma once

#include <cassert>

static bool IsPlanarResourceFormat(DXGI_FORMAT Format)
{
    bool Result = false;

    switch (Format)
    {
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        Result = true;
        break;
    }

    return Result;
}

static UINT16 NumSubresources(D3D12_RESOURCE_DESC *pDesc)
{
    UINT16 Result = 0;
    switch (pDesc->Dimension)
    {
    case D3D12_RESOURCE_DIMENSION_BUFFER:
        Result = 1;
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        Result = pDesc->DepthOrArraySize * pDesc->MipLevels;
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
        Result = pDesc->MipLevels;
        break;
    }

    if (IsPlanarResourceFormat(pDesc->Format))
    {
        Result *= 2;
    }

    return Result;
}

//------------------------------------------------------------------------------------------------
// Per-subresource layout tracking for Enhanced Barriers (textures only).
// Optimized for the common case where all subresources share the same layout.
struct SubresourceLayout
{
    // When true, all subresources share m_UniformLayout and m_PerSubresource is empty.
    bool m_AllSame = true;
    D3D12_BARRIER_LAYOUT m_UniformLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;
    std::vector<D3D12_BARRIER_LAYOUT> m_PerSubresource; // populated only when !m_AllSame

    SubresourceLayout() = default;
    SubresourceLayout(SubresourceLayout &&) = default;
    SubresourceLayout(const SubresourceLayout &) = default;
    SubresourceLayout &operator=(SubresourceLayout &&) = default;
    SubresourceLayout &operator=(const SubresourceLayout &) = default;

    // Initialize all subresources to the same layout.
    // LAYOUT_UNDEFINED and any uniform layout incur no heap allocation.
    SubresourceLayout(D3D12_BARRIER_LAYOUT layout, UINT /*numSubresources*/)
        : m_AllSame(true), m_UniformLayout(layout)
    {}

    D3D12_BARRIER_LAYOUT GetLayout(UINT Subresource) const
    {
        if (m_AllSame) return m_UniformLayout;
        return (Subresource < (UINT)m_PerSubresource.size())
            ? m_PerSubresource[Subresource]
            : D3D12_BARRIER_LAYOUT_UNDEFINED;
    }

    // Set layout for a subresource, or 0xFFFFFFFF for all subresources.
    // Handles uniform→per-subresource expansion transparently.
    // numSubresources is required when setting an individual subresource (for expansion).
    void SetLayout(UINT Subresource, D3D12_BARRIER_LAYOUT Layout, UINT numSubresources = 0)
    {
        if (Subresource == 0xFFFFFFFF)
        {
            m_AllSame = true;
            m_UniformLayout = Layout;
            m_PerSubresource.clear();
        }
        else
        {
            ExpandToPerSubresource(numSubresources);
            assert(Subresource < (UINT)m_PerSubresource.size());
            m_PerSubresource[Subresource] = Layout;
        }
    }

    // Try to collapse per-subresource tracking back to uniform if all entries match.
    void TryCollapse()
    {
        if (m_AllSame || m_PerSubresource.empty()) return;
        D3D12_BARRIER_LAYOUT first = m_PerSubresource[0];
        for (size_t i = 1; i < m_PerSubresource.size(); ++i)
        {
            if (m_PerSubresource[i] != first) return;
        }
        m_AllSame = true;
        m_UniformLayout = first;
        m_PerSubresource.clear();
    }

    // Expand from uniform to per-subresource tracking. No-op if already expanded.
    void ExpandToPerSubresource(UINT numSubresources)
    {
        if (m_AllSame)
        {
            m_PerSubresource.assign(numSubresources, m_UniformLayout);
            m_AllSame = false;
        }
    }
};

//------------------------------------------------------------------------------------------------
class CResource
{
public:
    CComPtr<ID3D12Resource> m_pD3DResource = nullptr;
    D3D12_RESOURCE_DESC m_Desc;
    UINT m_NumSubresources = 0;

    CResource(ID3D12Resource *pD3DResource) :
        m_pD3DResource(pD3DResource),
        m_Desc(pD3DResource->GetDesc()),
        m_NumSubresources(NumSubresources(&m_Desc))
    {
    }

    virtual ~CResource() = default;

    bool IsBuffer() const { return m_Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER; }

    void Rename(ID3D12Resource *pD3DResource) { m_pD3DResource = pD3DResource; }

    ID3D12Resource *GetD3DResource() { return m_pD3DResource; }

    UINT ArraySliceStride() { return m_Desc.MipLevels; }

    UINT GetSubresourceIndex(UINT16 MipLevel, UINT16 ArraySlice = 0, UINT8 Plane = 0)
    {
        switch (m_Desc.Dimension)
        {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            if (MipLevel > 0 || ArraySlice > 0 || Plane > 0)
            {
                return (UINT)-1; // error
            }
            return 0;

        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            return MipLevel + m_Desc.MipLevels + ArraySlice + Plane * ArraySliceStride();
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            if (Plane > 0)
            {
                return (UINT)-1; // error
            }
            return MipLevel;
        }

        return UINT(-1); // ???
    }
};

//------------------------------------------------------------------------------------------------
// Texture resources have per-subresource layout tracking for Enhanced Barriers.
class CTextureResource : public CResource
{
public:
    SubresourceLayout m_CurrentLayout;

    CTextureResource(ID3D12Resource *pD3DResource, D3D12_BARRIER_LAYOUT InitLayout) :
        CResource(pD3DResource),
        m_CurrentLayout(InitLayout, m_NumSubresources)
    {
        assert(!IsBuffer());
    }

    D3D12_BARRIER_LAYOUT GetCurSubresourceLayout(UINT Subresource) const
    {
        return m_CurrentLayout.GetLayout(Subresource);
    }
};
