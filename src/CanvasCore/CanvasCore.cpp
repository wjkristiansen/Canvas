// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
class CNamedCollection
{
public:
    using CollectionType = std::map<std::string, CComPtr<IUnknown>>;
    using IteratorType = CollectionType::iterator;
    CollectionType m_NamedObjects;
};

//------------------------------------------------------------------------------------------------
class CNamedCollectionIterator : 
    public INamedCollectionIterator,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CNamedCollectionIterator)
        COM_INTERFACE_ENTRY(INamedCollectionIterator)
        COM_INTERFACE_ENTRY(IIterator)
    END_COM_MAP()

    CComPtr<INamedCollection> m_pNamedCollection = nullptr;
    CNamedCollection::IteratorType m_it;
    CNamedCollection::CollectionType *m_pMap;

    CNamedCollectionIterator() = default;

    STDMETHOD(MoveNext)()
    {
        ++m_it;
        if (m_it == m_pMap->end())
        {
            return S_FALSE; // Report this is the end of the collection
        }

        return S_OK;
    }

    STDMETHOD_(PCSTR, GetCurrentName)()
    {
        return m_it->first.c_str();
    }

    STDMETHOD(GetCurrentObject)(REFIID riid, _COM_Outptr_ void **ppUnk)
    {
        return m_it->second->QueryInterface(riid, ppUnk);
    }

    STDMETHOD(Remove)()
    {
        m_it = m_pMap->erase(m_it);
        if (m_it == m_pMap->end())
        {
            return S_FALSE; // Report this is the end of the collection
        }

        return S_OK;
    }

    void Init(INamedCollection *pNamedCollection, CNamedCollection::CollectionType *pMap, CNamedCollection::IteratorType &it) throw()
    {
        m_pNamedCollection = pNamedCollection;
        m_pMap = pMap;
        m_it = it;
    }
};

//------------------------------------------------------------------------------------------------
class CSceneObject : 
    public ISceneObject,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CSceneObject)
        COM_INTERFACE_ENTRY(ISceneObject)
    END_COM_MAP()
};


template<class _Base>
class CNamedCollectionImpl :
    public CNamedCollection,
    public _Base
{
    STDMETHOD(Insert)(PCSTR pName, _In_ IUnknown *pUnk)
    {
        try
        {
            auto result = m_NamedObjects.emplace(pName, pUnk); // throw(std::bad_alloc)
            if (!result.second)
            {
                // Collision, no insertion
                return E_FAIL;
            }
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }

    STDMETHOD(Find)(PCSTR pName, REFIID riid, _COM_Outptr_ void **ppUnk)
    {
        *ppUnk = nullptr;

        auto it = m_NamedObjects.find(pName);
        if (it == m_NamedObjects.end())
        {
            // Not found
            return S_FALSE;
        }

        return it->second->QueryInterface(riid, ppUnk);
    }

    STDMETHOD(CreateIterator)(PCSTR pName, _COM_Outptr_ INamedCollectionIterator **ppIterator)
    {
        *ppIterator = nullptr;

        auto it = pName ? m_NamedObjects.find(pName) : m_NamedObjects.begin();

        try
        {
            CNamedCollectionIterator *pIterator = new CComObjectNoLock<CNamedCollectionIterator>(); // throw(std::bad_alloc)
            pIterator->Init(this, &m_NamedObjects, it);
            pIterator->AddRef();
            *ppIterator = pIterator;
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }

        if (it == m_NamedObjects.end())
        {
            // Valid iterator points to end of collection
            return S_FALSE;
        }

        return S_OK;
    }
};

//------------------------------------------------------------------------------------------------
class CScene :
    public CNamedCollectionImpl<IScene>,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CScene)
        COM_INTERFACE_ENTRY(IScene)
        COM_INTERFACE_ENTRY(INamedCollection)
    END_COM_MAP()
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

    STDMETHOD(CreateObject)(PCSTR pName, REFIID riid, _COM_Outptr_ void **ppSceneObject)
    {
        *ppSceneObject = nullptr;
        try
        {
            CSceneObject *pSceneObject = new CComObjectNoLock<CSceneObject>(); // throw(std::bad_alloc)
            *ppSceneObject = pSceneObject;
            pSceneObject->AddRef();
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }
};

//------------------------------------------------------------------------------------------------
class CMesh
{
};

//------------------------------------------------------------------------------------------------
class CMeshComponent
{
};

//------------------------------------------------------------------------------------------------
class CMeshObject :
    public CSceneObject,
    public CMeshComponent
{

};


//------------------------------------------------------------------------------------------------
class CCameraComponent :
    public CSceneObject
{

};

//------------------------------------------------------------------------------------------------
class CCameraObject :
    public CSceneObject,
    public CCameraComponent
{

};

//------------------------------------------------------------------------------------------------
class CLight :
    public CSceneObject
{

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
