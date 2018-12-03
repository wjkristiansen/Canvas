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
    public ISceneGraphNode,
    public CCanvasObjectBase
{
public:
    static Result CANVASAPI Create(OBJECT_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppObj);

public:
    using ElementMapType = std::unordered_map<InterfaceId, CComPtr<typename IGeneric>>;

    CObject *m_pParent = nullptr; // weak pointer
    CObject *m_pPrevSibling = nullptr; // weak pointer
    CObject *m_pLastChild = nullptr; // weak pointer
    CComPtr<CObject> m_pNextSibling;
    CComPtr<CObject> m_pFirstChild;

    ElementMapType m_Elements;

    CObject(OBJECT_ELEMENT_FLAGS flags);

//    CANVASMETHOD(FinalConstruct)();
    CANVASMETHOD(QueryInterface)(InterfaceId iid, void **ppUnk);
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
    CModelInstance(CObject *pNode) :
        CInnerGeneric(pNode)
    {}
};

//------------------------------------------------------------------------------------------------
class CCamera :
    public CInnerGeneric<ICamera>
{
public:
    CCamera(CObject *pNode) :
        CInnerGeneric(pNode)
    {}
};

//------------------------------------------------------------------------------------------------
class CLight :
    public CInnerGeneric<ILight>
{
public:
    CLight(CObject *pNode) :
        CInnerGeneric(pNode)
    {}
};

//------------------------------------------------------------------------------------------------
class CTransform :
    public CInnerGeneric<ITransform>
{
public:
    CTransform(CObject *pNode) :
        CInnerGeneric(pNode)
    {}
};

//------------------------------------------------------------------------------------------------
template<class _Base>
class CSceneNodeElement :
    public _Base
{
public:
    static Result Create(InterfaceId iid, void **ppObj, CObject *pNode)
    {
        if (!ppObj)
        {
            return Result::BadPointer;
        }

        *ppObj = nullptr;

        try
        {
            CComPtr<_Base> pObj = new CSceneNodeElement<_Base>(pNode); // throw(std::bad_alloc)
            return pObj->QueryInterface(iid, ppObj);
        }
        catch (CanvasError &e)
        {
            return e.Result();
        }

        return Result::Success;
    }

    CSceneNodeElement(CObject *pNode) :
        _Base(pNode) {}
};

//------------------------------------------------------------------------------------------------
class CScene :
    public IScene,
    public CCanvasObjectBase
{
public:
    CComPtr<CObject> m_pRootSceneGraphNode;

    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        return CCanvasObjectBase::QueryInterface(iid, ppObj);
    }

    CScene(CObject *pRootSceneGraphNode) :
        m_pRootSceneGraphNode(pRootSceneGraphNode)
    {
    }
};
