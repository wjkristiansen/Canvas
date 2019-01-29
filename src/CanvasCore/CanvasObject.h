//================================================================================================
// CanvasObject
//================================================================================================

#pragma once

// Node in a linked list using a sentinal node
class CCanvasListNode
{
public:
    CCanvasListNode() :
        m_pPrev(this), 
        m_pNext(this)
    {}
    ~CCanvasListNode()
    {
        // Remove this from the list
        Remove();
    }
    CCanvasListNode *m_pPrev;
    CCanvasListNode *m_pNext;

    void InsertAfter(CCanvasListNode *pPrev)
    {
        m_pNext = pPrev->m_pNext;
        m_pNext->m_pPrev = this;
        m_pPrev = pPrev;
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
};
