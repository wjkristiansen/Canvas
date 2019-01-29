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
    using _ListType = std::vector<TGemPtr<CSceneGraphNode>>;
    _ListType m_ChildList;

    CSceneGraphNode(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk) final;
    GEMMETHOD(AddChild)(_In_ XSceneGraphNode *pChild) final;
    GEMMETHOD(CreateChildIterator)(_Outptr_ XIterator **ppIterator) final;
};

//------------------------------------------------------------------------------------------------
class CSceneGraphNodeIterator :
    public XIterator,
    public CGenericBase
{
public:
    CSceneGraphNode::_ListType::iterator m_it;
    TGemPtr<CSceneGraphNode> m_pNode;

    CSceneGraphNodeIterator(CSceneGraphNode *pNode) :
        m_pNode(pNode)
    {
        m_it = pNode->m_ChildList.begin();
    }

    GEMMETHOD_(bool, IsAtEnd)() final
    {
        return m_it == m_pNode->m_ChildList.end();
    }

    GEMMETHOD(Reset)() final
    {
        m_it = m_pNode->m_ChildList.begin();
        return Result::Success;
    }

    GEMMETHOD(MoveNext)() final
    {
        if (m_it != m_pNode->m_ChildList.end())
        {
            ++m_it;
            return m_it != m_pNode->m_ChildList.end() ? Result::Success : Result::End;
        }

        return Result::End;
    }

    GEMMETHOD(MovePrev)() final
    {
        if (m_it != m_pNode->m_ChildList.begin())
        {
            --m_it;
            return Result::Success;
        }

        return Result::End;
    }

    GEMMETHOD(Select)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (m_it != m_pNode->m_ChildList.end())
        {
            return (*m_it)->QueryInterface(iid, ppObj);
        }

        return Result::End;
    }

    GEMMETHOD(Prune)() final
    {
        if (m_it != m_pNode->m_ChildList.end())
        {
            m_pNode->m_ChildList.erase(m_it);
            return Result::Success;
        }

        return Result::End;
    }
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk)
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
class CSceneGraphNodeObject :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    TInnerGeneric<CObjectName> m_ObjectName;
    TInnerGeneric<CSceneGraphNode> m_SceneGraphNode;
    TInnerGeneric<CTransform> m_Transform;

    CSceneGraphNodeObject(CCanvas *pCanvas, PCWSTR szName);

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XObjectName::IId == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }

        if (XSceneGraphNode::IId == iid)
        {
            return m_SceneGraphNode.InternalQueryInterface(iid, ppObj);
        }

        if (XTransform::IId == iid)
        {
            return m_Transform.InternalQueryInterface(iid, ppObj);
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};
