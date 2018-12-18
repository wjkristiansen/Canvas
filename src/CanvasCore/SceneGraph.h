//================================================================================================
// SceneGraph
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CSceneGraphNode :
    public XSceneGraphNode,
    public CInnerGenericBase
{
public:
    using _ListType = std::vector<TCanvasPtr<CSceneGraphNode>>;
    _ListType m_ChildList;

    CSceneGraphNode(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}

    GOMMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk) final;
    GOMMETHOD(AddChild)(_In_ XSceneGraphNode *pChild) final;
    GOMMETHOD(CreateChildIterator)(_Outptr_ XIterator **ppIterator) final;
};

//------------------------------------------------------------------------------------------------
class CSceneGraphNodeIterator :
    public XIterator,
    public CGenericBase
{
public:
    CSceneGraphNode::_ListType::iterator m_it;
    TCanvasPtr<CSceneGraphNode> m_pNode;

    CSceneGraphNodeIterator(CSceneGraphNode *pNode) :
        m_pNode(pNode)
    {
        m_it = pNode->m_ChildList.begin();
    }

    GOMMETHOD_(bool, IsAtEnd)() final
    {
        return m_it == m_pNode->m_ChildList.end();
    }

    GOMMETHOD(Reset)() final
    {
        m_it = m_pNode->m_ChildList.begin();
        return Result::Success;
    }

    GOMMETHOD(MoveNext)() final
    {
        if (m_it != m_pNode->m_ChildList.end())
        {
            ++m_it;
            return m_it != m_pNode->m_ChildList.end() ? Result::Success : Result::End;
        }

        return Result::End;
    }

    GOMMETHOD(MovePrev)() final
    {
        if (m_it != m_pNode->m_ChildList.begin())
        {
            --m_it;
            return Result::Success;
        }

        return Result::End;
    }

    GOMMETHOD(Select)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (m_it != m_pNode->m_ChildList.end())
        {
            return (*m_it)->QueryInterface(iid, ppObj);
        }

        return Result::End;
    }

    GOMMETHOD(Prune)() final
    {
        if (m_it != m_pNode->m_ChildList.end())
        {
            m_pNode->m_ChildList.erase(m_it);
            return Result::Success;
        }

        return Result::End;
    }
    GOMMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk)
    {
        if (iid == XIterator::IId)
        {
            *ppUnk = this;
            AddRef();
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppUnk);
    }
};

//------------------------------------------------------------------------------------------------
template <>
class TCanvasObject<ObjectType::SceneGraphNode> :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    TInnerGeneric<CObjectName> m_ObjectName;
    TInnerGeneric<CSceneGraphNode> m_SceneGraphNode;

    TCanvasObject(CCanvas *pCanvas, PCWSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_SceneGraphNode(this),
        m_ObjectName(this, szName, pCanvas)
    {}

    GOMMETHOD_(ObjectType, GetType)() const { return ObjectType::SceneGraphNode; }

    GOMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XObjectName::IId == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }

        if (XObjectName::IId == iid)
        {
            return m_SceneGraphNode.InternalQueryInterface(iid, ppObj);
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};
