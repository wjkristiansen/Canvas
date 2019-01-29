//================================================================================================
// CanvasObject
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
// Node in a linked list using a sentinal node.
// By default the node points back to itself.  
// During destruction, the node removes itself from a list.
// Sentinel nodes are used to indicate the list terminator.
// In a sentinel node, m_pPrev points to the end of the list
// and m_pNext points to the beginning.
template<class _Type>
class TAutoListNode
{
public:
    TAutoListNode(_Type value, TAutoListNode *pPrev) :
        m_Value(value),
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
    TAutoListNode *m_pPrev;
    TAutoListNode *m_pNext;
    
    using ValueType = _Type;
    ValueType m_Value;

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
};

//------------------------------------------------------------------------------------------------
template<class _Type>
class TAutoList
{
    TAutoListNode<_Type> m_Sentinel;

public:
    TAutoList() :
        m_Sentinel(_Type(), nullptr) {}
    const TAutoListNode<_Type> *GetEnd() const { return &m_Sentinel; }
    TAutoListNode<_Type> *GetFirst() { return m_Sentinel.m_pNext; }
    TAutoListNode<_Type> *GetLast() { return m_Sentinel.m_pPrev; }
};

//------------------------------------------------------------------------------------------------
class CCanvasObjectBase :
    public CGenericBase
{
    TAutoListNode<CCanvasObjectBase *> m_OutstandingNode;

public:
    CCanvasObjectBase(class CCanvas *pCanvas);
};