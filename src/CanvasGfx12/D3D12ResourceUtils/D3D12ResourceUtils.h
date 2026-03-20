//================================================================================================
// Resource
//================================================================================================

#pragma once

#include <cassert>
#include "LinkedList.h"

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
struct DesiredSubresourceLayout : public SubresourceLayout
{
    bool m_UniformDirty = false;
    std::vector<bool> m_PerSubresourceDirty; // populated only when !m_AllSame

    DesiredSubresourceLayout() = default;
    DesiredSubresourceLayout(D3D12_BARRIER_LAYOUT layout, UINT numSubresources)
        : SubresourceLayout(layout, numSubresources)
    {}

    bool IsDirty(UINT Subresource) const
    {
        if (m_AllSame) return m_UniformDirty;
        return (Subresource < (UINT)m_PerSubresourceDirty.size())
            ? m_PerSubresourceDirty[Subresource] : false;
    }

    // Set all subresources to the same layout. Returns true if anything changed.
    bool SetUniformLayout(D3D12_BARRIER_LAYOUT Layout)
    {
        bool changed = !m_AllSame || m_UniformLayout != Layout;
        m_AllSame = true;
        m_UniformLayout = Layout;
        m_PerSubresource.clear();
        m_PerSubresourceDirty.clear();
        m_UniformDirty = changed;
        return m_UniformDirty;
    }

    // Set layout for a single subresource. Caller must call ExpandToPerSubresource first.
    bool SetSubresourceLayout(UINT Subresource, D3D12_BARRIER_LAYOUT Layout)
    {
        assert(!m_AllSame && Subresource < (UINT)m_PerSubresource.size());
        bool changed = m_PerSubresource[Subresource] != Layout;
        m_PerSubresource[Subresource] = Layout;
        m_PerSubresourceDirty[Subresource] = changed;
        return changed;
    }

    // Override to also expand dirty tracking. No-op if already expanded.
    void ExpandToPerSubresource(UINT numSubresources)
    {
        if (m_AllSame)
        {
            m_PerSubresourceDirty.assign(numSubresources, m_UniformDirty);
            SubresourceLayout::ExpandToPerSubresource(numSubresources);
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
    bool m_IsSimultaneousAccess = false;
    bool m_HasUnresolvedStateTransitions = false;

    DECLARE_LIST_ELEMENT(CResource);

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
// Buffer resources should use CResource directly.
class CTextureResource : public CResource
{
public:
    SubresourceLayout m_CurrentLayout;
    DesiredSubresourceLayout m_DesiredLayout;

    CTextureResource(ID3D12Resource *pD3DResource, D3D12_BARRIER_LAYOUT InitLayout) :
        CResource(pD3DResource),
        m_CurrentLayout(InitLayout, m_NumSubresources),
        m_DesiredLayout(InitLayout, m_NumSubresources)
    {
        assert(!IsBuffer());
    }

    D3D12_BARRIER_LAYOUT GetCurSubresourceLayout(UINT Subresource) const
    {
        return m_CurrentLayout.GetLayout(Subresource);
    }

    void SetDesiredResourceLayout(class CResourceStateManager &ResourceStateManager, D3D12_BARRIER_LAYOUT Layout);
    void SetDesiredSubresourceLayout(class CResourceStateManager &ResourceStateManager, UINT Subresource, D3D12_BARRIER_LAYOUT Layout);
};

//------------------------------------------------------------------------------------------------
class CResourceStateManager
{
public:
    LinkedList::CLinkedList<CResource> m_TransitionList;

    CResourceStateManager()
    {}
    void ResolveResourceBarriers(std::vector<D3D12_TEXTURE_BARRIER> &Barriers);
};
