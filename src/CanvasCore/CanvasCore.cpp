// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
class CNamedCollection :
    public INamedCollection,
    public CComObjectRoot
{
    BEGIN_COM_MAP(CNamedCollection)
        COM_INTERFACE_ENTRY(INamedCollection)
    END_COM_MAP()

    using CollectionType = std::map<std::string, CComPtr<IUnknown>>;
    using IteratorType = CollectionType::iterator;
    CollectionType m_NamedObjects;

    STDMETHOD(Insert)(PCSTR pName, _In_ IUnknown *pUnk);
    STDMETHOD(Find)(PCSTR pName, REFIID riid, _COM_Outptr_ void **ppUnk);
    STDMETHOD(CreateIterator)(PCSTR pName, _COM_Outptr_ INamedCollectionIterator **ppIterator);
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

    CComPtr<CNamedCollection> m_pNamedCollection = nullptr;
    CNamedCollection::IteratorType m_it;

    CNamedCollectionIterator() = default;

    STDMETHOD(MoveNext)()
    {
        ++m_it;
        if (m_it == m_pNamedCollection->m_NamedObjects.end())
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
        m_it = m_pNamedCollection->m_NamedObjects.erase(m_it);
        if (m_it == m_pNamedCollection->m_NamedObjects.end())
        {
            return S_FALSE; // Report this is the end of the collection
        }

        return S_OK;
    }

    void Init(CNamedCollection *pNamedCollection, CNamedCollection::IteratorType &it) throw()
    {
        m_pNamedCollection = pNamedCollection;
        m_it = it;
    }
};


//------------------------------------------------------------------------------------------------
STDMETHODIMP CNamedCollection::Insert(PCSTR pName, _In_ IUnknown *pUnk)
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

//------------------------------------------------------------------------------------------------
STDMETHODIMP CNamedCollection::Find(PCSTR pName, REFIID riid, _COM_Outptr_ void **ppUnk)
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

//------------------------------------------------------------------------------------------------
STDMETHODIMP CNamedCollection::CreateIterator(PCSTR pName, _COM_Outptr_ INamedCollectionIterator **ppIterator)
{
    *ppIterator = nullptr;

    auto it = pName ? m_NamedObjects.find(pName) : m_NamedObjects.begin();

    try
    {
        CNamedCollectionIterator *pIterator = new CComObjectNoLock<CNamedCollectionIterator>(); // throw(std::bad_alloc)
        pIterator->Init(this, it);
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

//------------------------------------------------------------------------------------------------
class CScene :
    public IScene ,
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
    virtual HRESULT STDMETHODCALLTYPE CreateScene(REFIID riid, void **ppScene)
    {
        return E_NOTIMPL;
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
