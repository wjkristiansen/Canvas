//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CSceneGraphNode::CSceneGraphNode(CCanvas *pCanvas) :
    CTransform(),
    CObjectBase(pCanvas)
{
}

//------------------------------------------------------------------------------------------------
CScene::CScene(CCanvas *pCanvas, _In_z_ PCWSTR szName) :
    m_ObjectName(this, szName, pCanvas), // throw(Gem::GemError)
    CSceneGraphNode(pCanvas)
{
    TGemPtr<XSceneGraphNode> pNode;
    std::wstring RootNodeName = std::wstring(szName) + L"_Root";
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::AddChild(_In_ XSceneGraphNode *pChild)
{
    try
    {
        m_ChildList.emplace_back(pChild); // throw(std::bad_alloc)
    }
    catch (std::bad_alloc &)
    {
        m_pCanvas->Logger().LogError(L"Out of memory: CSceneGraphNode::AddChild");
        return Result::OutOfMemory;
    }
    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::CreateChildIterator(_Outptr_ XIterator **ppIterator)
{
    try
    {
        CSceneGraphNodeIterator *pIterator = new TGeneric<CSceneGraphNodeIterator>(m_ChildList); // throw(std::bad_alloc)
        pIterator->AddRef();
        *ppIterator = pIterator;
        return Result::Success;
    }
    catch (std::bad_alloc &)
    {
        m_pCanvas->Logger().LogError(L"Out of memory: CSceneGraphNode::CreateChildIterator");
        *ppIterator = nullptr;
        return Result::OutOfMemory;
    }
}
