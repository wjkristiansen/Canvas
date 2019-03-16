//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CSceneGraphNode::CSceneGraphNode(CCanvas *pCanvas, PCWSTR szName) :
    CObjectBase(pCanvas),
    m_Transform(this),
    m_ObjectName(this, szName, pCanvas)
{
}

//------------------------------------------------------------------------------------------------
CScene::CScene(CCanvas *pCanvas, _In_z_ PCWSTR szName) :
    CObjectBase(pCanvas),
    m_ObjectName(this, szName, pCanvas) // throw(Gem::GemError)
{
    TGemPtr<XSceneGraphNode> pNode;
    std::wstring RootNodeName = std::wstring(szName) + L"_Root";
    ThrowGemError(pCanvas->CreateSceneGraphNode(GEM_IID_PPV_ARGS(&pNode), RootNodeName.c_str()));
    m_pRootSceneGraphNode = pNode;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::InternalQueryInterface(InterfaceId iid, void **ppUnk)
{
    if (iid == XSceneGraphNode::IId)
    {
        *ppUnk = this;
        AddRef(); // This will actually AddRef the outer generic
        return Result::Success;
    }

    if (XName::IId == iid)
    {
        return m_ObjectName.InternalQueryInterface(iid, ppUnk);
    }

    if (XTransform::IId == iid)
    {
        return m_Transform.InternalQueryInterface(iid, ppUnk);
    }

    return __super::InternalQueryInterface(iid, ppUnk);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CSceneGraphNode::AddChild(_In_ XSceneGraphNode *pChild)
{
    try
    {
        m_ChildList.emplace_back(reinterpret_cast<CSceneGraphNode*>(pChild)); // throw(std::bad_alloc)
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
        CSceneGraphNodeIterator *pIterator = new TGeneric<CSceneGraphNodeIterator>(this); // throw(std::bad_alloc)
        pIterator->AddRef();
        *ppIterator = pIterator;
        return Result::Success;
    }
    catch (std::bad_alloc &)
    {
        m_pCanvas->Logger().LogError(L"Out of memory: CSceneGraphNode::CreateChildIterator");
        return Result::OutOfMemory;
    }
}
