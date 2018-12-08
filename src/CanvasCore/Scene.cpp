//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
class CSceneGraphIterator :
    public XIterator,
    public CGenericBase
{
public:
    CSceneGraphNode::ChildListType::iterator m_it;
    CCanvasPtr<CSceneGraphNode> m_pParentNode;

    CSceneGraphIterator(CSceneGraphNode *pParentNode) :
        m_pParentNode(pParentNode) 
    {
        m_it = pParentNode->m_Children.begin();
    }

    CANVASMETHOD(MoveNext)() final
    {
        if (m_it != m_pParentNode->m_Children.end())
        {
            ++m_it;

            return m_it == m_pParentNode->m_Children.end() ? Result::End : Result::Success;
        }

        return Result::End;
    }

    CANVASMETHOD(MovePrev)() final
    {
        if (m_it != m_pParentNode->m_Children.begin())
        {
            --m_it;

            return Result::Success;
        }

        return Result::End;
    }

    CANVASMETHOD(Select)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        auto pObj = *m_it;
        return pObj->QueryInterface(iid, ppObj);
    }

    CANVASMETHOD(Remove)() final
    {
        if (m_it != m_pParentNode->m_Children.end())
        {
            m_it = m_pParentNode->m_Children.erase(m_it);
            return m_it == m_pParentNode->m_Children.end() ? Result::End : Result::Success;
        }

        return Result::End;
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
    try
    {
        m_Children.emplace_back(pChild); // throw(std::bad_alloc)
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CSceneGraphNode::EnumChildren(_Inout_ XIterator **ppIterator)
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
