// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
class CModelInstance :
    public IModelInstance
{
};

//------------------------------------------------------------------------------------------------
class CCamera :
    public ICamera
{
};

//------------------------------------------------------------------------------------------------
class CLight :
    public ILight
{
};

//------------------------------------------------------------------------------------------------
class CTransform :
    public ITransform
{
};

//------------------------------------------------------------------------------------------------
class CSceneGraphNode :
    public ISceneGraphNode
{
public:
    using NodeMapType = std::unordered_map<std::string, CComPtr<typename ISceneGraphNode>>;

    NodeMapType m_ChildNodes;
    CSceneGraphNode *m_pParent; // weak pointer

    STDMETHOD(AddChild)(_In_ PCSTR pName, _In_ ISceneGraphNode *pSceneNode)
    {
        auto result = m_ChildNodes.emplace(pName, pSceneNode);
        return result.second ? S_OK : E_FAIL;
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
class CTransformNode :
    public CTransform,
    public CSceneGraphNode,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CTransformNode)
        COM_INTERFACE_ENTRY(ISceneGraphNode)
        COM_INTERFACE_ENTRY(ITransform)
    END_COM_MAP()
};

//------------------------------------------------------------------------------------------------
class CMeshNode :
    public CSceneGraphNode,
    public CTransform,
    public CModelInstance,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CMeshNode)
        COM_INTERFACE_ENTRY(ISceneGraphNode)
        COM_INTERFACE_ENTRY(ITransform)
        COM_INTERFACE_ENTRY(IModelInstance)
    END_COM_MAP()
};

//------------------------------------------------------------------------------------------------
class CCameraNode :
    public CSceneGraphNode,
    public CTransform,
    public CCamera,
    public CModelInstance, // For debug rendering of the camera
    public CComObjectRoot
{
    BEGIN_COM_MAP(CCameraNode)
        COM_INTERFACE_ENTRY(ISceneGraphNode)
        COM_INTERFACE_ENTRY(ITransform)
        COM_INTERFACE_ENTRY(IModelInstance)
        COM_INTERFACE_ENTRY(ICamera)
    END_COM_MAP()
};

//------------------------------------------------------------------------------------------------
class CLightNode :
    public CSceneGraphNode,
    public CTransform,
    public CLight,
    public CModelInstance, // For debug rendering of the light
    public CComObjectRoot
{
    BEGIN_COM_MAP(CLightNode)
        COM_INTERFACE_ENTRY(ISceneGraphNode)
        COM_INTERFACE_ENTRY(ITransform)
        COM_INTERFACE_ENTRY(IModelInstance)
        COM_INTERFACE_ENTRY(ILight)
    END_COM_MAP()
};

//------------------------------------------------------------------------------------------------
class CNullNode :
    public CSceneGraphNode,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CNullNode)
        COM_INTERFACE_ENTRY(ISceneGraphNode)
    END_COM_MAP()
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
        try
        {
            CComPtr<CSceneGraphNode> pNode = new CComObjectNoLock<CNullNode>(); // throw(std::bad_alloc)
            m_pRootSceneGraphNode = pNode;
        }
        catch(std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }

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

    STDMETHOD(CreateTransformNode)(PCSTR pName, REFIID riid, _COM_Outptr_ void **ppSceneGraphNode)
    {
        return CreateNode<CTransformNode>(pName, riid, ppSceneGraphNode);
    }

    STDMETHOD(CreateMeshNode)(PCSTR pName, REFIID riid, _COM_Outptr_ void **ppSceneGraphNode)
    {
        return CreateNode<CMeshNode>(pName, riid, ppSceneGraphNode);
    }

    STDMETHOD(CreateCameraNode)(PCSTR pName, REFIID riid, _COM_Outptr_ void **ppSceneGraphNode)
    {
        return CreateNode<CCameraNode>(pName, riid, ppSceneGraphNode);
    }

    STDMETHOD(CreateLightNode)(PCSTR pName, REFIID riid, _COM_Outptr_ void **ppSceneGraphNode)
    {
        return CreateNode<CLightNode>(pName, riid, ppSceneGraphNode);
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
