//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CSceneGraphNode::InternalQueryInterface(InterfaceId iid, void **ppUnk)
{
    if (iid == InterfaceId::XSceneGraphNode)
    {
        *ppUnk = this;
        AddRef(); // This will actually AddRef the outer generic
        return Result::Success;
    }

    return __super::InternalQueryInterface(iid, ppUnk);
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CSceneGraphNode::AddChild(_In_ XSceneGraphNode *pChild)
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
