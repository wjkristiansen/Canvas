//================================================================================================
// SceneGraph
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CSceneGraphNodeIterator :
    public XIterator,
    public CGenericBase
{
public:
    using _ListType = std::vector<TGemPtr<XSceneGraphNode>>;
    _ListType::iterator m_it;
    _ListType& m_List;

    CSceneGraphNodeIterator(_ListType &List) :
        m_List(List)
    {
        m_it = m_List.begin();
    }

    GEMMETHOD_(bool, IsAtEnd)() final
    {
        return m_it == m_List.end();
    }

    GEMMETHOD(Reset)() final
    {
        m_it = m_List.begin();
        return Result::Success;
    }

    GEMMETHOD(MoveNext)() final
    {
        if (m_it != m_List.end())
        {
            ++m_it;
            return m_it != m_List.end() ? Result::Success : Result::End;
        }

        return Result::End;
    }

    GEMMETHOD(MovePrev)() final
    {
        if (m_it != m_List.begin())
        {
            --m_it;
            return Result::Success;
        }

        return Result::End;
    }

    GEMMETHOD(Select)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (m_it != m_List.end())
        {
            return reinterpret_cast<XSceneGraphNode *>((*m_it).Get())->QueryInterface(iid, ppObj);
        }

        *ppObj = nullptr;

        return Result::End;
    }

    GEMMETHOD(Prune)() final
    {
        if (m_it != m_List.end())
        {
            m_List.erase(m_it);
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
template<class _Base>
class CSceneGraphNode :
    public CTransform<_Base>,
    public CObjectBase
{

    TInnerGeneric<CNameTag> m_NameTag;

public:
    using _ListType = CSceneGraphNodeIterator::_ListType;
    _ListType m_ChildList;

    CSceneGraphNode(CCanvas* pCanvas, PCWSTR szName) :
        CTransform<_Base>(),
        m_NameTag(this, pCanvas, szName),
        CObjectBase(pCanvas)
    {
    }

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XSceneGraphNode::IId == iid)
        {
            *ppObj = reinterpret_cast<XSceneGraphNode *>(this);
            this->AddRef();
            return Result::Success;
        }

        if (CanvasIId_XNameTag == iid)
        {
            return m_NameTag.InternalQueryInterface(iid, ppObj);
        }

        return CTransform<_Base>::InternalQueryInterface(iid, ppObj);
    }

    GEMMETHOD(AddChild)(_In_ XSceneGraphNode* pChild) final;
    GEMMETHOD(CreateChildIterator)(_Outptr_ XIterator** ppIterator) final;
};

