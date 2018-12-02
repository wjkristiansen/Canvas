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
class CSceneGraphNode :
    public ISceneGraphNode,
    public CCanvasObjectBase
{
public:
    static Result CANVASAPI Create(NODE_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppObj);

public:
    using ElementMapType = std::unordered_map<InterfaceId, CComPtr<typename IGeneric>>;

    CSceneGraphNode *m_pParent = nullptr; // weak pointer
    CSceneGraphNode *m_pPrevSibling = nullptr; // weak pointer
    CSceneGraphNode *m_pLastChild = nullptr; // weak pointer
    CComPtr<CSceneGraphNode> m_pNextSibling;
    CComPtr<CSceneGraphNode> m_pFirstChild;

    ElementMapType m_Elements;

    CSceneGraphNode(NODE_ELEMENT_FLAGS flags);

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

//template<class _T>
//Result CreateAggregateElement(InterfaceId iid, void **ppObj, CSceneGraphNode *pNode)
//{
//    *ppObj = nullptr;
//    try
//    {
//        CComPtr<CComAggObject<_T>> pObj;
//        CComAggObject<_T>::CreateInstance(pNode, &pObj); // throw(std::bad_alloc)
//        pObj->QueryInterface(iid, reinterpret_cast<void **>(ppObj));
//    }
//    catch(std::bad_alloc &)
//    {
//        return Result::OutOfMemory;
//    }
//    return Result::NotImplemented;
//}

//------------------------------------------------------------------------------------------------
class CModelInstance :
    public CInnerGeneric<IModelInstance>
{
public:
    CModelInstance(CSceneGraphNode *pNode) :
        CInnerGeneric(pNode)
    {}
};

//------------------------------------------------------------------------------------------------
class CCamera :
    public CInnerGeneric<ICamera>
{
public:
    CCamera(CSceneGraphNode *pNode) :
        CInnerGeneric(pNode)
    {}
};

//------------------------------------------------------------------------------------------------
class CLight :
    public CInnerGeneric<ILight>
{
public:
    CLight(CSceneGraphNode *pNode) :
        CInnerGeneric(pNode)
    {}
};

//------------------------------------------------------------------------------------------------
class CTransform :
    public CInnerGeneric<ITransform>
{
public:
    CTransform(CSceneGraphNode *pNode) :
        CInnerGeneric(pNode)
    {}
};

//------------------------------------------------------------------------------------------------
template<class _Base>
class CSceneNodeElement :
    public _Base
{
public:
    static Result Create(InterfaceId iid, void **ppObj, CSceneGraphNode *pNode)
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

    CSceneNodeElement(CSceneGraphNode *pNode) :
        _Base(pNode) {}
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
