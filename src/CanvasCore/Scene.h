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

    CANVASMETHOD_(bool, IsAtEnd)() final
    {
        return m_it == m_pNode->m_ChildList.end();
    }

    CANVASMETHOD(Reset)() final
    {
        m_it = m_pNode->m_ChildList.begin();
        return Result::Success;
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
    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk)
    {
        if (iid == InterfaceId::XIterator)
        {
            *ppUnk = this;
            AddRef();
            return Result::Success;
        }

        if(iid == InterfaceId::XGeneric)
        {
            *ppUnk = reinterpret_cast<XGeneric *>(this);
            AddRef();
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppUnk);
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
    public CCanvasObjectBase
{
public:
    CInnerGeneric<CSceneGraphNode> m_RootSceneGraphNode;
    CInnerGeneric<CObjectName> m_ObjectName;

    CScene(CCanvas *pCanvas, _In_z_ PCSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_RootSceneGraphNode(this),
        m_ObjectName(this, szName, pCanvas)
    {
    }

    CANVASMETHOD_(ObjectType, GetType)() const final { return ObjectType::Scene; }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (InterfaceId::XScene == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }

        if (InterfaceId::XObjectName == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }

        if (InterfaceId::XSceneGraphNode == iid)
        {
            return m_RootSceneGraphNode.InternalQueryInterface(iid, ppObj);
        }
        return __super::InternalQueryInterface(iid, ppObj);
    }
};
