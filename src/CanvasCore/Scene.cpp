//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CSceneGraphNode::Remove()
{
    if(m_pPrevSibling)
    {
        m_pPrevSibling->m_pNextSibling = m_pNextSibling;
    }

    if (m_pNextSibling)
    {
        m_pNextSibling->m_pPrevSibling;
    }

    if (m_pParent)
    {
        if (m_pParent->m_pFirstChild == this)
        {
            m_pParent->m_pFirstChild = m_pNextSibling;
        }

        if (m_pParent->m_pLastChild == this)
        {
            m_pParent->m_pLastChild = m_pPrevSibling;
        }
    }

    m_pParent = nullptr;
    m_pPrevSibling = nullptr;
    m_pNextSibling = nullptr;
    return Result::NotImplemented;
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CSceneGraphNode::Insert(_In_opt_ XSceneGraphNode *pParent, _In_opt_ XSceneGraphNode *pInsertBefore)
{
    CSceneGraphNode *pParentNodeImp = reinterpret_cast<CSceneGraphNode *>(pParent);
    CSceneGraphNode *pInsertBeforeImp = pInsertBefore ? reinterpret_cast<CSceneGraphNode *>(pInsertBefore) : nullptr;
    if (pInsertBeforeImp && pInsertBeforeImp->m_pParent != pParent)
    {
        // Node parameters are not parent-child
        return Result::InvalidArg;
    }

    m_pNextSibling = pInsertBeforeImp;

    if (pInsertBeforeImp)
    {
        m_pPrevSibling = pInsertBeforeImp->m_pPrevSibling;
        pInsertBeforeImp->m_pPrevSibling = this;
        m_pParent = pParentNodeImp;
    }
    else
    {
        // Insert at the end of the list
        m_pPrevSibling = pParentNodeImp->m_pLastChild;
        if (pParentNodeImp->m_pLastChild)
        {
            pParentNodeImp->m_pLastChild->m_pNextSibling = this;
        }
        pParentNodeImp->m_pLastChild = this;
    }

    if (m_pPrevSibling)
    {
        m_pPrevSibling->m_pNextSibling = this;
    }
    else
    {
        pParentNodeImp->m_pFirstChild = this;
    }

    return Result::Success;
}
