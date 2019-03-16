//================================================================================================
// CanvasObject
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
template<class _T>
class TStaticPtr
{
    _T *m_ptr = nullptr;
public:
    TStaticPtr() = default;
    TStaticPtr(_T *p) :
        m_ptr(p) {}

    _T *Ptr() { return m_ptr; }
    const _T *Ptr() const { return m_ptr; }
};

//------------------------------------------------------------------------------------------------
// Node in a linked list using a sentinal node.
// By default the node points back to itself.  
// During destruction, the node removes itself from a list.
// Sentinel nodes are used to indicate the list terminator.
// In a sentinel node, m_pPrev points to the end of the list
// and m_pNext points to the beginning.
template<class _Base>
class TAutoListNode : public _Base
{
    TAutoListNode *m_pPrev;
    TAutoListNode *m_pNext;
    
public:
    template<typename ... Args>
    TAutoListNode(TAutoListNode *pPrev, Args... args) :
        _Base(args...),
        m_pPrev(this),
        m_pNext(this)
    {
        if (pPrev)
        {
            m_pPrev = pPrev;
            m_pNext = pPrev->m_pNext;
            pPrev->m_pNext->m_pPrev = this;
            pPrev->m_pNext = this;
        }
    }
    ~TAutoListNode()
    {
        // Remove this from the list
        Remove();
    }

    void Remove()
    {
        // Can't remove the sentinel node
        if (m_pNext != this)
        {
            m_pPrev->m_pNext = m_pNext;
            m_pNext->m_pPrev = m_pPrev;

            // Point to self
            m_pNext = this;
            m_pPrev = this;
        }
    }

    TAutoListNode *GetPrev() const { return m_pPrev; }
    TAutoListNode *GetNext() const { return m_pNext; }
};

//------------------------------------------------------------------------------------------------
template<class _NodeBaseType>
class TAutoList
{
    TAutoListNode<_NodeBaseType> m_Sentinel;

public:
    TAutoList() :
        m_Sentinel(nullptr) {}
    const TAutoListNode<_NodeBaseType> *GetEnd() const { return &m_Sentinel; }
    TAutoListNode<_NodeBaseType> *GetFirst() { return m_Sentinel.GetNext(); }
    TAutoListNode<_NodeBaseType> *GetLast() { return m_Sentinel.GetPrev(); }
};

//------------------------------------------------------------------------------------------------
class CCanvasObjectBase :
    public CGenericBase
{
public:
    class CCanvas *m_pCanvas; // Weak pointer

    TAutoListNode<TStaticPtr<CCanvasObjectBase>> m_OutstandingNode;

    CCanvasObjectBase(class CCanvas *pCanvas);
};
