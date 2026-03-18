//================================================================================================
// CResource
//================================================================================================

#include "pch.h"

#include "D3D12ResourceUtils.h"


//------------------------------------------------------------------------------------------------
void CTextureResource::SetDesiredResourceLayout(CResourceStateManager &ResourceStateManager, D3D12_BARRIER_LAYOUT Layout)
{
    SetDesiredSubresourceLayout(ResourceStateManager, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, Layout);
}

//------------------------------------------------------------------------------------------------
void CTextureResource::SetDesiredSubresourceLayout(CResourceStateManager &ResourceStateManager, UINT Subresource, D3D12_BARRIER_LAYOUT Layout)
{
    bool dirty;
    if (Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        dirty = m_DesiredLayout.SetUniformLayout(Layout);
    }
    else
    {
        m_CurrentLayout.ExpandToPerSubresource(m_NumSubresources);
        m_DesiredLayout.ExpandToPerSubresource(m_NumSubresources);
        dirty = m_DesiredLayout.SetSubresourceLayout(Subresource, Layout);
    }

    if (dirty && !m_HasUnresolvedStateTransitions)
    {
        ResourceStateManager.m_TransitionList.PushTail(this);
        m_HasUnresolvedStateTransitions = true;
    }
}

//------------------------------------------------------------------------------------------------
static void AddTextureLayoutBarrier(std::vector<D3D12_TEXTURE_BARRIER> &Barriers, ID3D12Resource *pResource, UINT Subresource, D3D12_BARRIER_LAYOUT LayoutBefore, D3D12_BARRIER_LAYOUT LayoutAfter)
{
    D3D12_TEXTURE_BARRIER b = {};
    b.pResource = pResource;
    b.LayoutBefore = LayoutBefore;
    b.LayoutAfter = LayoutAfter;
    b.SyncBefore = D3D12_BARRIER_SYNC_ALL;
    b.SyncAfter = D3D12_BARRIER_SYNC_ALL;
    b.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
    b.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
    b.Subresources.IndexOrFirstMipLevel = (Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) ? 0xFFFFFFFF : Subresource;
    b.Subresources.NumMipLevels = 0;
    b.Subresources.FirstArraySlice = 0;
    b.Subresources.NumArraySlices = 0;
    b.Subresources.FirstPlane = 0;
    b.Subresources.NumPlanes = 0;
    b.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
    Barriers.push_back(b);
}

//------------------------------------------------------------------------------------------------
void CResourceStateManager::ResolveResourceBarriers(std::vector<D3D12_TEXTURE_BARRIER> &Barriers)
{
    for (CResource *pBase = m_TransitionList.PopHead(); pBase; pBase = m_TransitionList.PopHead())
    {
        assert(pBase->m_HasUnresolvedStateTransitions);
        assert(!pBase->IsBuffer());

        // Only texture resources should be in the transition list
        auto *pResource = static_cast<CTextureResource*>(pBase);
        auto &cur     = pResource->m_CurrentLayout;
        auto &desired = pResource->m_DesiredLayout;

        if (desired.m_AllSame && cur.m_AllSame)
        {
            // Both uniform: at most one barrier needed
            if (cur.m_UniformLayout != desired.m_UniformLayout)
            {
                AddTextureLayoutBarrier(Barriers, pResource->m_pD3DResource,
                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    cur.m_UniformLayout, desired.m_UniformLayout);
            }
            cur.m_UniformLayout  = desired.m_UniformLayout;
            desired.m_UniformDirty = false;
        }
        else
        {
            // At least one side is per-subresource: iterate all subresources
            UINT n = pResource->m_NumSubresources;

            // Ensure current has per-subresource slots so we can update them
            cur.ExpandToPerSubresource(n);

            bool ResultUniform = true;
            D3D12_BARRIER_LAYOUT ResultSentinel = D3D12_BARRIER_LAYOUT_UNDEFINED;

            for (UINT i = 0; i < n; ++i)
            {
                D3D12_BARRIER_LAYOUT CurLayout     = cur.m_PerSubresource[i];
                D3D12_BARRIER_LAYOUT DesiredLayout = desired.GetLayout(i);

                if (desired.IsDirty(i))
                {
                    if (CurLayout != DesiredLayout)
                        AddTextureLayoutBarrier(Barriers, pResource->m_pD3DResource, i, CurLayout, DesiredLayout);
                    cur.m_PerSubresource[i] = DesiredLayout;
                    if (!desired.m_AllSame)
                        desired.m_PerSubresourceDirty[i] = false;
                    CurLayout = DesiredLayout;
                }

                if (ResultUniform && i > 0 && ResultSentinel != CurLayout)
                    ResultUniform = false;
                ResultSentinel = CurLayout;
            }

            if (desired.m_AllSame)
                desired.m_UniformDirty = false;

            // Collapse current back to uniform if all subresources now share the same layout
            if (ResultUniform)
            {
                cur.m_AllSame = true;
                cur.m_UniformLayout = ResultSentinel;
                cur.m_PerSubresource.clear();
            }
        }

        pBase->m_HasUnresolvedStateTransitions = false;
    }
}
