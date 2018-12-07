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
    CSceneGraphNode *m_pParent = nullptr; // weak pointer
    CSceneGraphNode *m_pPrevSibling = nullptr; // weak pointer
    CSceneGraphNode *m_pLastChild = nullptr; // weak pointer
    CCanvasPtr<CSceneGraphNode> m_pNextSibling;
    CCanvasPtr<CSceneGraphNode> m_pFirstChild;

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

    CANVASMETHOD(Insert)(_In_ XSceneGraphNode *pParent, _In_opt_ XSceneGraphNode *pInsertBefore);
    CANVASMETHOD(Remove)();
    CANVASMETHOD_(XSceneGraphNode *, GetParent)() { return m_pParent; }
    CANVASMETHOD_(XSceneGraphNode *, GetFirstChild)() { return m_pFirstChild; }
    CANVASMETHOD_(XSceneGraphNode *, GetLastChild)() { return m_pLastChild; }
    CANVASMETHOD_(XSceneGraphNode *, GetPrevSibling)() { return m_pPrevSibling; }
    CANVASMETHOD_(XSceneGraphNode *, GetNextSibling)() { return m_pNextSibling; }
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

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
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
