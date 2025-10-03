//================================================================================================
// CResource
//================================================================================================

#include "pch.h"

#include "D3D12ResourceUtils.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
static void InitTransitionBarrier(D3D12_RESOURCE_BARRIER &Barrier, ID3D12Resource *pD3DResource, UINT Subresource, D3D12_RESOURCE_STATES StateBefore, D3D12_RESOURCE_STATES StateAfter)
{
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    Barrier.Transition.pResource = pD3DResource;
    Barrier.Transition.Subresource = Subresource;
    Barrier.Transition.StateBefore = StateBefore;
    Barrier.Transition.StateAfter = StateAfter;
}

//------------------------------------------------------------------------------------------------
static bool IsGenericReadState(D3D12_RESOURCE_STATES State)
{
    return State != D3D12_RESOURCE_STATE_COMMON && !!(State & D3D12_RESOURCE_STATE_GENERIC_READ);
}

//------------------------------------------------------------------------------------------------
// If State and InitState can be combined into a single state then return
// the bitwise combination of both states.
// Otherwise return State
bool DesiredResourceState::SetState(D3D12_RESOURCE_STATES State, UINT Subresource)
{
    if (Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        Subresource = 0;
        AllSubresourcesSame = true;
    }
    else
    {
        if (AllSubresourcesSame)
        {
            // Copy the uniform desired state to all other desired states
            std::fill(
                SubresourceStates.begin() + 1,
                SubresourceStates.end(),
                SubresourceStates[0]);

            std::fill(
                SubresourceStateDirty.begin() + 1,
                SubresourceStateDirty.end(),
                SubresourceStateDirty[0]);
        }

        AllSubresourcesSame = false;
    }

    if (SubresourceStateDirty[Subresource])
    {
        // Treat only GENERIC_READ states as accumulatable
        if (IsGenericReadState(State) && IsGenericReadState(SubresourceStates[Subresource]))
        {
            State = State | SubresourceStates[Subresource];
        }
    }

    // Only set dirty flag if the desired state is changed
    SubresourceStateDirty[Subresource] = SubresourceStates[Subresource] != State;
    SubresourceStates[Subresource] = State;

    return SubresourceStateDirty[Subresource];
}

//------------------------------------------------------------------------------------------------
void CResource::SetDesiredResourceState(CResourceStateManager &ResourceStateManager, D3D12_RESOURCE_STATES State)
{
    SetDesiredSubresourceState(ResourceStateManager, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, State);
}

//------------------------------------------------------------------------------------------------
void CResource::SetDesiredSubresourceState(CResourceStateManager &ResourceStateManager, UINT Subresource, D3D12_RESOURCE_STATES State)
{
    // Is this all-subresources?
    bool dirty = m_DesiredResourceState.SetState(State, Subresource);

    if (dirty)
    {
        if (Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES && m_ResourceState.AllSubresourcesSame)
        {
            // Copy the uniform state to all other states
            std::fill(
                m_ResourceState.SubresourceStates.begin() + 1,
                m_ResourceState.SubresourceStates.end(),
                m_ResourceState.SubresourceStates[0]);
        }

        // Add the resource to the transition list
        if (!m_HasUnresolvedStateTransitions)
        {
            ResourceStateManager.m_TransitionList.PushTail(this);
            m_HasUnresolvedStateTransitions = true;
        }
    }
}

//------------------------------------------------------------------------------------------------
static bool IsTransitionNeeded(D3D12_RESOURCE_STATES CurState, D3D12_RESOURCE_STATES DesiredState)
{
    if (CurState == DesiredState)
    {
        return false;
    }

    // Is the desired state already part of the current state
    if (DesiredState != D3D12_RESOURCE_STATE_COMMON && (CurState & DesiredState) == DesiredState)
    {
        return false;
    }

    // BUGBUG: Some simultaneous-access stuff here...

    return true;
}

//------------------------------------------------------------------------------------------------
static void AddResourceTransitionBarrier(std::vector<D3D12_RESOURCE_BARRIER> &Barriers, ID3D12Resource *pResource, UINT Subresource, D3D12_RESOURCE_STATES StateBefore, D3D12_RESOURCE_STATES StateAfter)
{
    Barriers.emplace_back(D3D12_RESOURCE_BARRIER{});
    Barriers.back().Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barriers.back().Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    Barriers.back().Transition.pResource = pResource;
    Barriers.back().Transition.StateBefore = StateBefore;
    Barriers.back().Transition.StateAfter = StateAfter;
    Barriers.back().Transition.Subresource = Subresource;
}

//------------------------------------------------------------------------------------------------
void CResourceStateManager::ResolveResourceBarriers(std::vector<D3D12_RESOURCE_BARRIER> &Barriers)
{
    // For each transitioning resource
    for (CResource *pResource = m_TransitionList.PopHead(); pResource; pResource = m_TransitionList.PopHead())
    {
        assert(pResource->m_HasUnresolvedStateTransitions);

        if (pResource->m_DesiredResourceState.AllSubresourcesSame && pResource->m_ResourceState.AllSubresourcesSame)
        {
            auto CurState = pResource->m_ResourceState.SubresourceStates[0];
            auto DesiredState = pResource->m_DesiredResourceState.SubresourceStates[0];
            if (IsTransitionNeeded(CurState, DesiredState))
            {
                AddResourceTransitionBarrier(Barriers, pResource->m_pD3DResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, CurState, DesiredState);
            }
            pResource->m_ResourceState.AllSubresourcesSame = true;
            pResource->m_ResourceState.SubresourceStates[0] = DesiredState;
            pResource->m_DesiredResourceState.SubresourceStateDirty[0] = false;
        }
        else
        {
            bool Uniform = true;
            D3D12_RESOURCE_STATES UniformSentinelState = D3D12_RESOURCE_STATE_COMMON;

            for (UINT i = 0; i < pResource->m_NumSubresources; ++i)
            {
                auto CurState = pResource->m_ResourceState.SubresourceStates[i];
                if (pResource->m_DesiredResourceState.SubresourceStateDirty[i])
                {
                    auto DesiredState = pResource->m_DesiredResourceState.SubresourceStates[i];
                    if (IsTransitionNeeded(CurState, DesiredState))
                    {
                        AddResourceTransitionBarrier(Barriers, pResource->m_pD3DResource, i, CurState, DesiredState);
                    }
                    pResource->m_ResourceState.SubresourceStates[i] = DesiredState;
                    pResource->m_DesiredResourceState.SubresourceStateDirty[i] = false;
                    CurState = DesiredState;
                }

                // Keep track of 
                if (Uniform && i > 0 && UniformSentinelState != CurState)
                {
                    Uniform = false;
                }
                UniformSentinelState = CurState;
            }
            pResource->m_ResourceState.AllSubresourcesSame = Uniform;
        }
        pResource->m_HasUnresolvedStateTransitions = false;
    }
}

}