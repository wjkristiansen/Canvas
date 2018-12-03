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
class CObject :
    public IGeneric,
    public CCanvasObjectBase
{
    using ElementMapType = std::unordered_map<InterfaceId, CComPtr<typename IGeneric>>;
    ElementMapType m_Elements;

public:
    static Result CANVASAPI Create(OBJECT_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppObj);
    CObject(OBJECT_ELEMENT_FLAGS flags);
    CANVASMETHOD(QueryInterface)(InterfaceId iid, void **ppUnk);
};

//------------------------------------------------------------------------------------------------
class CSceneGraphNode :
    public CInnerGeneric<ISceneGraphNode>
{
public:
    CSceneGraphNode(CObject *pObj) :
        CInnerGeneric(pObj)
    {}

    CSceneGraphNode *m_pParent = nullptr; // weak pointer
    CSceneGraphNode *m_pPrevSibling = nullptr; // weak pointer
    CSceneGraphNode *m_pLastChild = nullptr; // weak pointer
    CComPtr<CSceneGraphNode> m_pNextSibling;
    CComPtr<CSceneGraphNode> m_pFirstChild;

//    CANVASMETHOD(FinalConstruct)();
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
    public CInnerGeneric<IModelInstance>
{
public:
    CModelInstance(CObject *pObj) :
        CInnerGeneric(pObj)
    {}
};

//------------------------------------------------------------------------------------------------
class CCamera :
    public CInnerGeneric<ICamera>
{
public:
    CCamera(CObject *pObj) :
        CInnerGeneric(pObj)
    {}
};

//------------------------------------------------------------------------------------------------
class CLight :
    public CInnerGeneric<ILight>
{
public:
    CLight(CObject *pObj) :
        CInnerGeneric(pObj)
    {}
};

//------------------------------------------------------------------------------------------------
class CTransform :
    public CInnerGeneric<ITransform>
{
public:
    CTransform(CObject *pObj) :
        CInnerGeneric(pObj)
    {}
};

//------------------------------------------------------------------------------------------------
template<class _Base>
class CObjectElement :
    public _Base
{
public:
    static Result Create(InterfaceId iid, void **ppObj, CObject *pObj)
    {
        if (!ppObj)
        {
            return Result::BadPointer;
        }

        *ppObj = nullptr;

        try
        {
            CComPtr<_Base> pElement = new CObjectElement<_Base>(pObj); // throw(std::bad_alloc)
            return pElement->QueryInterface(iid, ppObj);
        }
        catch (CanvasError &e)
        {
            return e.Result();
        }

        return Result::Success;
    }

    CObjectElement(CObject *pObj) :
        _Base(pObj) {}
};

//------------------------------------------------------------------------------------------------
class CScene :
    public IScene,
    public CCanvasObjectBase
{
public:
    CComPtr<CSceneGraphNode> m_pRootSceneGraphNode;

    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        return CCanvasObjectBase::QueryInterface(iid, ppObj);
    }

    CScene(CSceneGraphNode *pRootSceneGraphNode) :
        m_pRootSceneGraphNode(pRootSceneGraphNode)
    {
    }
};
