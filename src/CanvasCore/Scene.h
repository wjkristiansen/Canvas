//================================================================================================
// Scene
//================================================================================================

#pragma once

namespace std
{
    template<>
    class hash<InterfaceId>
    {
    public:
        size_t operator()(InterfaceId iid) const
        {
            return static_cast<size_t>(iid);
        }
    };
}

//------------------------------------------------------------------------------------------------
class CInnerGenericBase
{
public:
    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        return Result::NoInterface;
    }
};

//------------------------------------------------------------------------------------------------
class CObject :
    public IGeneric,
    public CGenericBase
{
    using ElementMapType = std::unordered_map<InterfaceId, std::unique_ptr<CInnerGenericBase>>;
    ElementMapType m_Elements;

public:
    static Result CANVASAPI Create(OBJECT_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppObj);
    CObject(OBJECT_ELEMENT_FLAGS flags);
    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk);
};

//------------------------------------------------------------------------------------------------
class CSceneGraphNode :
    public ISceneGraphNode,
    public CInnerGenericBase
{
public:
    CSceneGraphNode *m_pParent = nullptr; // weak pointer
    CSceneGraphNode *m_pPrevSibling = nullptr; // weak pointer
    CSceneGraphNode *m_pLastChild = nullptr; // weak pointer
    CComPtr<CSceneGraphNode> m_pNextSibling;
    CComPtr<CSceneGraphNode> m_pFirstChild;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, void **ppUnk)
    {
        if (iid == InterfaceId::ISceneGraphNode)
        {
            *ppUnk = this;
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppUnk);
    }

    CANVASMETHOD(Insert)(_In_ ISceneGraphNode *pParent, _In_opt_ ISceneGraphNode *pInsertBefore);
    CANVASMETHOD(Remove)();
    CANVASMETHOD_(ISceneGraphNode *, GetParent)() { return m_pParent; }
    CANVASMETHOD_(ISceneGraphNode *, GetFirstChild)() { return m_pFirstChild; }
    CANVASMETHOD_(ISceneGraphNode *, GetLastChild)() { return m_pLastChild; }
    CANVASMETHOD_(ISceneGraphNode *, GetPrevSibling)() { return m_pPrevSibling; }
    CANVASMETHOD_(ISceneGraphNode *, GetNextSibling)() { return m_pNextSibling; }
};

//------------------------------------------------------------------------------------------------
class CModelInstance :
    public IModelInstance,
    public CInnerGenericBase
{
public:
};

//------------------------------------------------------------------------------------------------
class CCamera :
    public ICamera,
    public CInnerGenericBase
{
};

//------------------------------------------------------------------------------------------------
class CLight :
    public ILight,
    public CInnerGenericBase
{
};

//------------------------------------------------------------------------------------------------
class CTransform :
    public ITransform,
    public CInnerGenericBase
{
};

//------------------------------------------------------------------------------------------------
class CScene :
    public IScene,
    public CGenericBase
{
public:
    CComPtr<CSceneGraphNode> m_pRootSceneGraphNode;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (InterfaceId::IScene == iid)
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
