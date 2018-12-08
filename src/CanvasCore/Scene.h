//================================================================================================
// Scene
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CSceneGraphNode :
    public XSceneGraphNode,
    public CGenericBase
{
public:
    using ChildListType = std::list<CCanvasPtr<XSceneGraphNode>>;
    ChildListType m_Children;

    class CChildIterator :
        public XIterator,
        public CGenericBase
    {
    public:
        ChildListType::iterator m_it;
        CCanvasPtr<CSceneGraphNode> m_pParentNode;

        CChildIterator(CSceneGraphNode *pParentNode) :
            m_pParentNode(pParentNode) 
        {
            m_it = pParentNode->m_Children.begin();
        }

        CANVASMETHOD(MoveNext)() final
        {
            if (m_it == m_pParentNode->m_Children.end())
            {
                return Result::End;
            }
            m_it++;

            return m_it == m_pParentNode->m_Children.end() ? Result::End : Result::Success;
        }

        CANVASMETHOD(MovePrev)() final
        {
            if (m_it == m_pParentNode->m_Children.begin())
            {
                return Result::End;
            }

            --m_it;

            return Result::Success;
        }

        CANVASMETHOD(GetElement)(InterfaceId iid, _Outptr_ void **ppObj)
        {
            auto pObj = *m_it;
            return pObj->QueryInterface(iid, ppObj);
        }

    };

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk) final
    {
        if (iid == InterfaceId::XSceneGraphNode)
        {
            *ppUnk = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppUnk);
    }

    CANVASMETHOD(AddChild)(_In_ XSceneGraphNode *pChild)
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

    CANVASMETHOD(EnumChildren)(_Inout_ XIterator **ppIterator)
    {
        *ppIterator = nullptr;

        if (!ppIterator)
        {
            return Result::BadPointer;
        }

        try
        {
            CChildIterator *pIterator = new CGeneric<CChildIterator>(this); // throw(std::bad_alloc)
            pIterator->AddRef();
            *ppIterator = pIterator;
        }
        catch (std::bad_alloc &)
        {
            return Result::OutOfMemory;
        }

        return Result::Success;
    }
};

//------------------------------------------------------------------------------------------------
class CModelInstance :
    public XModelInstance,
    public CGenericBase
{
public:
    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XModelInstance == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

//------------------------------------------------------------------------------------------------
class CCamera :
    public XCamera,
    public CGenericBase
{
public:
    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XCamera == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

//------------------------------------------------------------------------------------------------
class CLight :
    public XLight,
    public CGenericBase
{
public:
    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XLight == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

//------------------------------------------------------------------------------------------------
class CTransform :
    public XTransform,
    public CGenericBase
{
public:
    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XTransform == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

//------------------------------------------------------------------------------------------------
class CScene :
    public XScene,
    public CGenericBase
{
public:
    CInnerGeneric<CSceneGraphNode> m_RootSceneGraphNode;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (InterfaceId::XScene == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        if (InterfaceId::XSceneGraphNode == iid)
        {
            *ppObj = &m_RootSceneGraphNode;
            AddRef();
            return Result::Success;
        }
        return __super::InternalQueryInterface(iid, ppObj);
    }

    CScene(CSceneGraphNode *pRootSceneGraphNode) :
        m_RootSceneGraphNode(this)
    {
    }
};
