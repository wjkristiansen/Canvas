//================================================================================================
// Scene
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CSceneGraphNode :
    public XSceneGraphNode,
    public CInnerGenericBase
{
public:
    using _ListType = std::vector<CCanvasPtr<CSceneGraphNode>>;
    _ListType m_ChildList;

    CSceneGraphNode(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk) final;
    CANVASMETHOD(AddChild)(_In_ XSceneGraphNode *pChild) final;
    CANVASMETHOD(CreateChildIterator)(_Outptr_ XIterator **ppIterator) final;
};

//------------------------------------------------------------------------------------------------
class CSceneGraphNodeIterator :
    public XIterator,
    public CGenericBase
{
public:
    CSceneGraphNode::_ListType::iterator m_it;
    CCanvasPtr<CSceneGraphNode> m_pNode;

    CSceneGraphNodeIterator(CSceneGraphNode *pNode) :
        m_pNode(pNode)
    {
        m_it = pNode->m_ChildList.begin();
    }

    CANVASMETHOD(MoveNext)() final
    {
        if (m_it != m_pNode->m_ChildList.end())
        {
            ++m_it;
            return m_it != m_pNode->m_ChildList.end() ? Result::Success : Result::End;
        }

        return Result::End;
    }

    CANVASMETHOD(MovePrev)() final
    {
        if (m_it != m_pNode->m_ChildList.begin())
        {
            --m_it;
            return Result::Success;
        }

        return Result::End;
    }

    CANVASMETHOD(Select)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (m_it != m_pNode->m_ChildList.end())
        {
            return (*m_it)->QueryInterface(iid, ppObj);
        }

        return Result::End;
    }

    CANVASMETHOD(Prune)() final
    {
        if (m_it != m_pNode->m_ChildList.end())
        {
            m_pNode->m_ChildList.erase(m_it);
            return Result::Success;
        }

        return Result::End;
    }
};

//------------------------------------------------------------------------------------------------
class CModelInstance :
    public XModelInstance,
    public CInnerGenericBase
{
public:
    CModelInstance(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}
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
    public CInnerGenericBase
{
public:
    CCamera(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}
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
    public CInnerGenericBase
{
public:
    CLight(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}
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
    public CInnerGenericBase
{
public:
    CTransform(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}
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

//------------------------------------------------------------------------------------------------
class CCustomObject :
    public CCanvasObjectBase
{
public:
    CCustomObject(CCanvas *pCanvas) : 
        CCanvasObjectBase(pCanvas)
    {}

    // For now just create a vector of inner element pointers.  Consider
    // in the future allocating a contiguous chunk of memory for all
    // elements (including the outer CObject interface) and using
    // placement-new to allocate the whole object
    std::vector<std::unique_ptr<CGenericBase>> m_InnerElements;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk) final
    {
        for (auto &pElement : m_InnerElements)
        {
            Result res = pElement->InternalQueryInterface(iid, ppUnk);
            if (Result::NoInterface != res)
            {
                return res;
            }
        }

        return Result::NoInterface;
    }
};
