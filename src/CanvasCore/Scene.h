//================================================================================================
// Scene
//================================================================================================

#pragma once

namespace std
{
    template<>
    class hash<IID>
    {
    public:
        size_t operator()(REFIID riid) const
        {
            return riid.Data1;
        }
    };
}

//------------------------------------------------------------------------------------------------
class CSceneGraphNodeBase :
    public Canvas::ISceneGraphNode,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CSceneGraphNodeBase)
        COM_INTERFACE_ENTRY(Canvas::ISceneGraphNode)
    END_COM_MAP()

};


//------------------------------------------------------------------------------------------------
class CSceneGraphNode :
    public CComObjectNoLock<CSceneGraphNodeBase>
{
public:
    Canvas::NodeElementFlags m_NodeElementFlags = Canvas::NodeElementFlags::None;
    using NodeMapType = std::unordered_map<std::string, CComPtr<typename Canvas::ISceneGraphNode>>;
    using ElementMapType = std::unordered_map<IID, CComPtr<typename IUnknown>>;

    NodeMapType m_ChildNodes;
    CSceneGraphNode *m_pParent; // weak pointer

    ElementMapType m_Elements;

    CSceneGraphNode(Canvas::NodeElementFlags flags) : CComObjectNoLock<CSceneGraphNodeBase>(),
        m_NodeElementFlags(flags)
    {
    }

    STDMETHOD(FinalConstruct)();
    STDMETHOD(QueryInterface)(REFIID riid, void **ppUnk);
    STDMETHOD(AddChild)(_In_ PCSTR pName, _In_ Canvas::ISceneGraphNode *pSceneNode);
};

template<class _T>
HRESULT CreateAggregateElement(REFIID riid, void **ppObj, CSceneGraphNode *pNode)
{
    *ppObj = nullptr;
    try
    {
        CComPtr<CComAggObject<_T>> pObj;
        CComAggObject<_T>::CreateInstance(pNode, &pObj); // throw(std::bad_alloc)
        pObj->QueryInterface(riid, reinterpret_cast<void **>(ppObj));
    }
    catch(std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return E_NOTIMPL;
}

//------------------------------------------------------------------------------------------------
class CModelInstance :
    public Canvas::IModelInstance,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CModelInstance)
        COM_INTERFACE_ENTRY(IModelInstance)
    END_COM_MAP()

    static HRESULT Create(REFIID riid, void **ppModelInstance, CSceneGraphNode *pNode);
};

//------------------------------------------------------------------------------------------------
class CCamera :
    public Canvas::ICamera,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CCamera)
        COM_INTERFACE_ENTRY(Canvas::ICamera)
    END_COM_MAP()

    static HRESULT Create(REFIID riid, void **ppCamera, CSceneGraphNode *pNode);
};

//------------------------------------------------------------------------------------------------
class CLight :
    public Canvas::ILight,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CLight)
        COM_INTERFACE_ENTRY(Canvas::ILight)
    END_COM_MAP()

    static HRESULT Create(REFIID riid, void **ppLight, CSceneGraphNode *pNode);
};

//------------------------------------------------------------------------------------------------
class CTransform :
    public Canvas::ITransform,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CTransform)
        COM_INTERFACE_ENTRY(Canvas::ITransform)
    END_COM_MAP()

    static HRESULT Create(REFIID riid, void **ppTransform, CSceneGraphNode *pNode);
};

//------------------------------------------------------------------------------------------------
class CSceneGraphIterator :
    public Canvas::ISceneGraphIterator
{
    CComPtr<CSceneGraphNode> m_pContainingSceneGraphNode;
    CSceneGraphNode::NodeMapType::iterator m_It;

    STDMETHOD(MoveNextSibling)();
    STDMETHOD(Reset)(_In_ Canvas::ISceneGraphNode *pParentNode, _In_opt_ PCSTR pName);
    STDMETHOD(GetNode(REFIID riid, void **ppNode));
};

//------------------------------------------------------------------------------------------------
class CScene :
    public Canvas::IScene,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CScene)
        COM_INTERFACE_ENTRY(Canvas::IScene)
    END_COM_MAP()
    
    CComPtr<CSceneGraphNode> m_pRootSceneGraphNode;
    STDMETHOD(FinalConstruct)();
};
