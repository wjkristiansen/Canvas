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
    CSceneGraphNode *pChildImpl = reinterpret_cast<CSceneGraphNode *>(pChild);
    pChildImpl->m_pPrevSibling = *m_ppChildTail;
    *m_ppChildTail = pChildImpl;
    m_ppChildTail = &pChildImpl->m_pNextSibling;
    pChildImpl->AddRef();

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CSceneGraphNode::Prune()
{
    if (m_pPrevSibling)
    {
        m_pPrevSibling->m_pNextSibling = m_pNextSibling;
    }
    else if(m_pParent)
    {
        m_pParent->m_pFirstChild = m_pNextSibling;
    }

    if (m_pNextSibling)
    {
        m_pNextSibling->m_pPrevSibling = m_pPrevSibling;
    }
    else if(m_pParent)
    {
        if(m_pPrevSibling)
        {
            m_pParent->m_ppChildTail = &m_pPrevSibling->m_pNextSibling;
        }
        else
        {
            m_pParent->m_ppChildTail = &m_pParent->m_pFirstChild;
        }
    }

    m_pPrevSibling = nullptr;
    m_pNextSibling = nullptr;
    m_pParent = nullptr;

    return Result::Success;
}
