//================================================================================================
// Resource
//================================================================================================

#pragma once

#include "LinkedList.h"

namespace Canvas
{

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
struct ResourceState
{
    bool AllSubresourcesSame = false;
    std::vector<D3D12_RESOURCE_STATES> SubresourceStates;

    ResourceState() = default;
    ResourceState(ResourceState &&o) = default;
    ResourceState(const ResourceState &o) = default;

    ResourceState &operator=(ResourceState &&o) = default;
    ResourceState &operator=(const ResourceState &o) = default;

    ResourceState(D3D12_RESOURCE_STATES InitState, UINT NumSubresources) :
        AllSubresourcesSame(true),
        SubresourceStates(NumSubresources)
    {
        std::fill(SubresourceStates.begin(), SubresourceStates.end(), InitState);
    }
};

//------------------------------------------------------------------------------------------------
struct DesiredResourceState : public ResourceState
{
    std::vector<bool> SubresourceStateDirty;

    DesiredResourceState(D3D12_RESOURCE_STATES InitState, UINT NumSubresources) :
        ResourceState(InitState, NumSubresources),
        SubresourceStateDirty(NumSubresources)
    {}

    bool IsStateDirty(UINT Subresource)
    {
        return AllSubresourcesSame ? SubresourceStateDirty[0] : SubresourceStateDirty[Subresource];
    }

    bool SetState(D3D12_RESOURCE_STATES State, UINT Subresource);
};

//------------------------------------------------------------------------------------------------
class CResource
{
public:
    CComPtr<ID3D12Resource> m_pD3DResource = nullptr;
    D3D12_RESOURCE_DESC m_Desc;
    UINT m_NumSubresources = 0;
    ResourceState m_ResourceState;
    DesiredResourceState m_DesiredResourceState;
    bool m_IsSimultaneousAccess = false;
    bool m_HasUnresolvedStateTransitions = false;

    DECLARE_LIST_ELEMENT(CResource);

    CResource(ID3D12Resource *pD3DResource, D3D12_RESOURCE_STATES InitState) :
        m_pD3DResource(pD3DResource),
        m_Desc(pD3DResource->GetDesc()),
        m_NumSubresources(NumSubresources(&m_Desc)),
        m_ResourceState(InitState, m_NumSubresources),
        m_DesiredResourceState(InitState, m_NumSubresources)
    {
    }

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

    D3D12_RESOURCE_STATES GetCurSubresourceState(UINT Subresource) const
    {
        return m_ResourceState.AllSubresourcesSame ? m_ResourceState.SubresourceStates[0] : m_ResourceState.SubresourceStates[Subresource];
    }

    void SetDesiredResourceState(class CResourceStateManager &ResourceStateManager, D3D12_RESOURCE_STATES State);
    void SetDesiredSubresourceState(class CResourceStateManager &ResourceStateManager, UINT Subresource, D3D12_RESOURCE_STATES State);
};

//------------------------------------------------------------------------------------------------
class CResourceStateManager
{
public:
    LinkedList::CLinkedList<CResource> m_TransitionList;

    CResourceStateManager()
    {}
    void ResolveResourceBarriers(std::vector<D3D12_RESOURCE_BARRIER> &Barriers);
};

}