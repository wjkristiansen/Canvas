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
class CSceneGraphNodeBase :
    public ISceneGraphNode,
    public CCanvasObjectBase
{
public:
    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        return CCanvasObjectBase::QueryInterface(iid, ppObj);
    }
};


//------------------------------------------------------------------------------------------------
class CSceneGraphNode :
    public CGeneric<CSceneGraphNodeBase>
{
public:
    NODE_ELEMENT_FLAGS m_NodeElementFlags = NODE_ELEMENT_FLAGS_NONE;
    using NodeMapType = std::unordered_map<std::string, CComPtr<typename ISceneGraphNode>>;
    using ElementMapType = std::unordered_map<InterfaceId, CComPtr<typename IGeneric>>;

    NodeMapType m_ChildNodes;
    CSceneGraphNode *m_pParent; // weak pointer

    ElementMapType m_Elements;

    CSceneGraphNode(NODE_ELEMENT_FLAGS flags) : CGeneric<CSceneGraphNodeBase>(),
        m_NodeElementFlags(flags)
    {
    }

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
    public IModelInstance,
    public CCanvasObjectBase
{
public:
    static Result Create(InterfaceId iid, void **ppModelInstance, CSceneGraphNode *pNode);
};

//------------------------------------------------------------------------------------------------
class CCamera :
    public ICamera,
    public CCanvasObjectBase
{
public:
    static Result Create(InterfaceId iid, void **ppCamera, CSceneGraphNode *pNode);
};

//------------------------------------------------------------------------------------------------
class CLight :
    public ILight,
    public CCanvasObjectBase
{
public:
    static Result Create(InterfaceId iid, void **ppLight, CSceneGraphNode *pNode);
};

//------------------------------------------------------------------------------------------------
class CTransform :
    public ITransform,
    public CCanvasObjectBase
{
public:
    static Result Create(InterfaceId iid, void **ppTransform, CSceneGraphNode *pNode);
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
//    CANVASMETHOD(FinalConstruct)();
};
