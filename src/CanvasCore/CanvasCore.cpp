// CanvasCore.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace Canvas;
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

    STDMETHOD(CreateNode)(PCSTR pName, NodeElementFlags flags, REFIID riid, _COM_Outptr_ void **ppSceneGraphNode)
    {
        *ppSceneGraphNode = nullptr;
        try
        {
            CComPtr<CSceneGraphNode> pSceneGraphNode = new CSceneGraphNode(flags); // throw(std::bad_alloc)
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
            CCanvas *pCanvas = new CComObjectNoLock<CCanvas>; // throw(bad_alloc)
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
