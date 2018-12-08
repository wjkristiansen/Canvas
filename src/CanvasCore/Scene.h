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

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk) final;
    CANVASMETHOD(AddChild)(_In_ XSceneGraphNode *pChild) final;
    CANVASMETHOD(EnumChildren)(_Inout_ XIterator **ppIterator) final;
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
