#pragma once

//------------------------------------------------------------------------------------------------
template<class _Base>
GEMMETHODIMP TSceneGraphNode<_Base>::AddChild(_In_ XSceneGraphNode* pChild)
{
    try
    {
        m_ChildList.emplace_back(pChild); // throw(std::bad_alloc)
    }
    catch (std::bad_alloc&)
    {
        m_pCanvas->Logger().LogError("Out of memory: TSceneGraphNode::AddChild");
        return Result::OutOfMemory;
    }
    return Result::Success;
}

//------------------------------------------------------------------------------------------------
template<class _Base>
GEMMETHODIMP TSceneGraphNode<_Base>::CreateChildIterator(_Outptr_result_nullonfailure_ XIterator** ppIterator)
{
    try
    {
        TSceneGraphNodeIterator* pIterator = new TGeneric<TSceneGraphNodeIterator>(m_ChildList); // throw(std::bad_alloc)
        pIterator->AddRef();
        *ppIterator = pIterator;
        return Result::Success;
    }
    catch (std::bad_alloc&)
    {
        m_pCanvas->Logger().LogError("Out of memory: TSceneGraphNode::CreateChildIterator");
        *ppIterator = nullptr;
        return Result::OutOfMemory;
    }
}

