// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;

namespace std
{
    template<>
    class hash<IID>
    {
    public:
        size_t operator()(REFIID riid) const
        {
            return riid.Data1;
            //const UINT64 *pData = reinterpret_cast<const UINT64 *>(&riid);
            //UINT64 hash64 = pData[0] ^ pData[1];
            //return hash64;
        }
    };
}

//------------------------------------------------------------------------------------------------
class CSceneGraphNodeBase :
    public ISceneGraphNode,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CSceneGraphNodeBase)
        COM_INTERFACE_ENTRY(ISceneGraphNode)
    END_COM_MAP()

};

//------------------------------------------------------------------------------------------------
class CSceneGraphNode :
    public CSceneGraphNodeBase
{
public:
    using NodeMapType = std::unordered_map<std::string, CComPtr<typename ISceneGraphNode>>;
    using ElementMapType = std::unordered_map<IID, CComPtr<typename IUnknown>>;

    NodeMapType m_ChildNodes;
    CSceneGraphNode *m_pParent; // weak pointer

    ElementMapType m_Elements;

    STDMETHOD(QueryInterface)(REFIID riid, void **ppUnk)
    {
        if (riid == __uuidof(ITransform))
        {
            auto it = m_Elements.find(riid);
            if (it == m_Elements.end())
            {
                return E_NOINTERFACE;
            }
        }
    }

    STDMETHOD(AddChild)(_In_ PCSTR pName, _In_ ISceneGraphNode *pSceneNode)
    {
        auto result = m_ChildNodes.emplace(pName, pSceneNode);
        return result.second ? S_OK : E_FAIL;
    }
};

template<class _T>
HRESULT CreateAggregateElement(REFIID riid, _T **ppObj, CSceneGraphNode *pNode)
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
    public IModelInstance,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CModelInstance)
        COM_INTERFACE_ENTRY(IModelInstance)
    END_COM_MAP()

    static HRESULT Create(REFIID riid, CModelInstance **ppModelInstance, CSceneGraphNode *pNode)
    {
        try
        {
            ThrowFailure(CreateAggregateElement<CModelInstance>(riid, ppModelInstance, pNode));
        }
        catch (_com_error &e)
        {
            return e.Error();
        }
    }
};

//------------------------------------------------------------------------------------------------
class CCamera :
    public ICamera,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CCamera)
        COM_INTERFACE_ENTRY(ICamera)
    END_COM_MAP()

    static HRESULT Create(REFIID riid, CCamera **ppCamera, CSceneGraphNode *pNode)
    {
        try
        {
            ThrowFailure(CreateAggregateElement(riid, ppCamera, pNode));
        }
        catch (_com_error &e)
        {
            return e.Error();
        }
    }
};

//------------------------------------------------------------------------------------------------
class CLight :
    public ILight,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CLight)
        COM_INTERFACE_ENTRY(ILight)
    END_COM_MAP()

    static HRESULT Create(REFIID riid, CLight **ppLight, CSceneGraphNode *pNode)
    {
        try
        {
            ThrowFailure(CreateAggregateElement(riid, ppLight, pNode));
        }
        catch (_com_error &e)
        {
            return e.Error();
        }
    }
};

//------------------------------------------------------------------------------------------------
class CTransform :
    public ITransform,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CTransform)
        COM_INTERFACE_ENTRY(ITransform)
    END_COM_MAP()

    static HRESULT Create(REFIID riid, CTransform **ppTransform, CSceneGraphNode *pNode)
    {
        try
        {
            ThrowFailure(CreateAggregateElement(riid, ppTransform, pNode));
        }
        catch (_com_error &e)
        {
            return e.Error();
        }
    }
};

//------------------------------------------------------------------------------------------------
class CSceneGraphIterator :
    public ISceneGraphIterator
{
    CComPtr<CSceneGraphNode> m_pContainingSceneGraphNode;
    CSceneGraphNode::NodeMapType::iterator m_It;

    STDMETHOD(MoveNextSibling)()
    {
        if (m_It != m_pContainingSceneGraphNode->m_ChildNodes.end())
        {
            ++m_It;
            if (m_It != m_pContainingSceneGraphNode->m_ChildNodes.end())
            {
                return S_OK;
            }
        }

        return S_FALSE;
    }

    STDMETHOD(Reset)(_In_ ISceneGraphNode *pParentNode, _In_opt_ PCSTR pName)
    {
        HRESULT hr = S_OK;
        CSceneGraphNode *pParentNodeImpl = static_cast<CSceneGraphNode *>(pParentNode);
        CSceneGraphNode::NodeMapType::iterator it;
        if(pName)
        {
            it = pParentNodeImpl->m_ChildNodes.find(pName);
        }
        else
        {
            it = pParentNodeImpl->m_ChildNodes.begin();
        }

        if(it == pParentNodeImpl->m_ChildNodes.end())
        {
            hr = S_FALSE;
        }

        m_It = it;

        m_pContainingSceneGraphNode = pParentNodeImpl;

        return hr;
    }

    STDMETHOD(GetNode(REFIID riid, void **ppNode))
    {
        if(m_pContainingSceneGraphNode)
        {
            return m_pContainingSceneGraphNode->QueryInterface(riid, ppNode);
        }
        else
        {
            return E_FAIL;
        }
    }

};

//------------------------------------------------------------------------------------------------
class CScene :
    public IScene,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CScene)
        COM_INTERFACE_ENTRY(IScene)
    END_COM_MAP()
    
    CComPtr<CSceneGraphNode> m_pRootSceneGraphNode;
    STDMETHOD(FinalConstruct)()
    {
        return S_OK;
    }
};

//------------------------------------------------------------------------------------------------
class CCanvas
    : public ICanvas
    , public CComObjectRoot
{
    BEGIN_COM_MAP(CCanvas)
        COM_INTERFACE_ENTRY(ICanvas)
    END_COM_MAP()

public:
    CCanvas() = default;
    STDMETHOD(CreateScene)(REFIID riid, void **ppScene)
    {
        *ppScene = nullptr;
        try
        {
            CScene *pScene = new CComObjectNoLock<CScene>(); // throw(std::bad_alloc)
            *ppScene = pScene;
            pScene->AddRef();
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }

    template<class _T>
    HRESULT CreateNode(PCSTR pName, REFIID riid, _COM_Outptr_ void **ppSceneGraphNode)
    {
        *ppSceneGraphNode = nullptr;
        try
        {
            CComPtr<_T> pSceneGraphNode = new CComObjectNoLock<_T>(); // throw(std::bad_alloc)
            *ppSceneGraphNode = pSceneGraphNode;
            return pSceneGraphNode->QueryInterface(riid, ppSceneGraphNode);
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
    }
};

HRESULT STDMETHODCALLTYPE CreateCanvas(REFIID riid, void **ppCanvas)
{
    *ppCanvas = nullptr;

    try
    {
        if (riid == __uuidof(ICanvas))
        {
            CCanvas *pCanvas = new CComObjectNoLock<CCanvas>(); // throw(bad_alloc)
            *ppCanvas = pCanvas;
            pCanvas->AddRef();
        }
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}
