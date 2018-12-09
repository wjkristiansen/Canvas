//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

template<class _NodeType>
class CTreeIterator
{
public:
    std::deque<_NodeType *> m_Stack;

    CANVASMETHOD(MoveNext)()
    {
        return Result::NotImplemented;
    }
    CANVASMETHOD(MovePrev)()
    {
        return Result::NotImplemented;
    }
};

//------------------------------------------------------------------------------------------------
class CSceneGraphIterator :
    public XTreeIterator,
    public CGenericBase
{
public:
    CSceneGraphNode *m_pCurrent; // weak-ptr

    CSceneGraphIterator(_In_ CSceneGraphNode *pNode) :
        m_pCurrent(pNode) 
    {
    }

    CANVASMETHOD(MoveNext)() final
    {
        if (m_pCurrent->m_pNextSibling)
        {
            m_pCurrent = m_pCurrent->m_pNextSibling;
            return Result::Success;
        }

        return Result::End;
    }

    CANVASMETHOD(MovePrev)() final
    {
        if (m_pCurrent->m_pPrevSibling)
        {
            m_pCurrent = m_pCurrent->m_pPrevSibling;
            return Result::Success;
        }

        return Result::End;
    }

    CANVASMETHOD(MoveParent)() final
    {
        if (m_pCurrent->m_pParent)
        {
            m_pCurrent = m_pCurrent->m_pParent;
            return Result::Success;
        }

        return Result::End;
    }

    CANVASMETHOD(MoveFirstChild)() final
    {
        if (m_pCurrent->m_pFirstChild)
        {
            m_pCurrent = m_pCurrent->m_pFirstChild;
            return Result::Success;
        }

        return Result::End;
    }

    CANVASMETHOD(Select)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        return m_pCurrent->QueryInterface(iid, ppObj);
    }
};

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
CANVASMETHODIMP CSceneGraphNode::MakeIterator(_Inout_ XTreeIterator **ppIterator)
{
    *ppIterator = nullptr;

    if (!ppIterator)
    {
        return Result::BadPointer;
    }

    try
    {
        CSceneGraphIterator *pIterator = new CGeneric<CSceneGraphIterator>(this); // throw(std::bad_alloc)
        pIterator->AddRef();
        *ppIterator = pIterator;
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }

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
