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
    CComPtr<CSceneGraphNode> m_pNextSibling;
    CComPtr<CSceneGraphNode> m_pFirstChild;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk)
    {
        if (iid == InterfaceId::XSceneGraphNode)
        {
            *ppUnk = this;
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
};

//------------------------------------------------------------------------------------------------
class CCamera :
    public XCamera,
    public CGenericBase
{
};

//------------------------------------------------------------------------------------------------
class CLight :
    public XLight,
    public CGenericBase
{
};

//------------------------------------------------------------------------------------------------
class CTransform :
    public XTransform,
    public CGenericBase
{
};

//------------------------------------------------------------------------------------------------
class CScene :
    public XScene,
    public CGenericBase
{
public:
    CComPtr<CSceneGraphNode> m_pRootSceneGraphNode;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (InterfaceId::XScene == iid)
        {
            *ppObj = this;
            AddRef();
            return Result::Success;
        }
        return __super::InternalQueryInterface(iid, ppObj);
    }

    CScene(CSceneGraphNode *pRootSceneGraphNode) :
        m_pRootSceneGraphNode(pRootSceneGraphNode)
    {
    }
};
