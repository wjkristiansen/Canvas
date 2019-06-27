#pragma once

//------------------------------------------------------------------------------------------------
template<class _Base>
GEMMETHODIMP CSceneGraphNode<_Base>::AddChild(_In_ XSceneGraphNode* pChild)
{
    try
    {
        m_ChildList.emplace_back(pChild); // throw(std::bad_alloc)
    }
    catch (std::bad_alloc&)
    {
        m_pCanvas->Logger().LogError(L"Out of memory: CSceneGraphNode::AddChild");
        return Result::OutOfMemory;
    }
    return Result::Success;
}

//------------------------------------------------------------------------------------------------
template<class _Base>
GEMMETHODIMP CSceneGraphNode<_Base>::CreateChildIterator(_Outptr_ XIterator** ppIterator)
{
    try
    {
        CSceneGraphNodeIterator* pIterator = new TGeneric<CSceneGraphNodeIterator>(m_ChildList); // throw(std::bad_alloc)
        pIterator->AddRef();
        *ppIterator = pIterator;
        return Result::Success;
    }
    catch (std::bad_alloc&)
    {
        m_pCanvas->Logger().LogError(L"Out of memory: CSceneGraphNode::CreateChildIterator");
        *ppIterator = nullptr;
        return Result::OutOfMemory;
    }
}

