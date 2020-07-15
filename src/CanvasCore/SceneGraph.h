//================================================================================================
// SceneGraph
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class TSceneGraphNodeIterator :
    public XIterator,
    public CGenericBase
{
public:
    using _ListType = std::vector<TGemPtr<XSceneGraphNode>>;
    _ListType::iterator m_it;
    _ListType& m_List;

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XIterator)
    END_GEM_INTERFACE_MAP()

    TSceneGraphNodeIterator(_ListType &List) :
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

    GEMMETHOD(Select)(InterfaceId iid, _Outptr_result_maybenull_ void **ppObj) final
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
};

//------------------------------------------------------------------------------------------------
template<class _Base>
class TSceneGraphNode :
    public TTransform<_Base>,
    public CObjectBase
{
protected:
    TInnerGeneric<CNameTag> m_NameTag;

public:
    using _ListType = TSceneGraphNodeIterator::_ListType;
    _ListType m_ChildList;

    TSceneGraphNode(CCanvas* pCanvas, PCSTR szName) :
        TTransform<_Base>(),
        m_NameTag(this, pCanvas, szName),
        CObjectBase(pCanvas)
    {
    }

    GEMMETHOD(AddChild)(_In_ XSceneGraphNode* pChild) final;
    GEMMETHOD(CreateChildIterator)(_Outptr_result_nullonfailure_ XIterator** ppIterator) final;

	virtual Gem::InterfaceId GetMostDerivedType() const { return _Base::IId; }
};


//------------------------------------------------------------------------------------------------
class CNullSceneGraphNode :
    public TSceneGraphNode<XSceneGraphNode>
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XTransform)
        GEM_INTERFACE_ENTRY(XSceneGraphNode)
        GEM_CONTAINED_INTERFACE_ENTRY(XNameTag, m_NameTag)
    END_GEM_INTERFACE_MAP()

    CNullSceneGraphNode(CCanvas *pCanvas, PCSTR szName) :
        TSceneGraphNode<XSceneGraphNode>(pCanvas, szName) {
    }
};
