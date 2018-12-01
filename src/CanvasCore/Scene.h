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
    using NodeMapType = std::unordered_map<std::string, CComPtr<typename ISceneGraphNode>>;
    using ElementMapType = std::unordered_map<InterfaceId, CComPtr<typename IGeneric>>;

    NodeMapType m_ChildNodes;
    CSceneGraphNode *m_pParent; // weak pointer

    ElementMapType m_Elements;

    CSceneGraphNode(NODE_ELEMENT_FLAGS flags);

//    CANVASMETHOD(FinalConstruct)();
    CANVASMETHOD(QueryInterface)(InterfaceId iid, void **ppUnk);
    CANVASMETHOD(AddChild)(_In_ PCSTR pName, _In_ ISceneGraphNode *pSceneNode);
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
class CSceneGraphIterator :
    public ISceneGraphIterator,
    public CCanvasObjectBase
{
public:
    CComPtr<CSceneGraphNode> m_pContainingSceneGraphNode;
    CSceneGraphNode::NodeMapType::iterator m_It;

    CANVASMETHOD(MoveNextSibling)();
    CANVASMETHOD(Reset)(_In_ ISceneGraphNode *pParentNode, _In_opt_ PCSTR pName);
    CANVASMETHOD(GetNode(InterfaceId iid, void **ppNode));
    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        return CCanvasObjectBase::QueryInterface(iid, ppObj);
    }
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
