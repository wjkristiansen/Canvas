//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CSceneGraphNodeObject::CSceneGraphNodeObject(CCanvas *pCanvas, PCWSTR szName) :
    CGenericBase(),
    CCanvasListNode(),
    m_SceneGraphNode(this),
    m_Transform(this),
    m_ObjectName(this, szName, pCanvas)
{
    pCanvas->AddOutstandingObject(this);
}

//------------------------------------------------------------------------------------------------
CScene::CScene(CCanvas *pCanvas, _In_z_ PCWSTR szName) :
    m_ObjectName(this, szName, pCanvas),
    m_RootSceneGraphNode(this),
    CGenericBase(),
    CCanvasListNode()
{
    pCanvas->AddOutstandingObject(this);
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
        return Result::OutOfMemory;
    }
}
