//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

STDMETHODIMP CSceneGraphNode::FinalConstruct()
{
    try
    {
        ThrowFailure(__super::FinalConstruct());

        if (m_NodeElementFlags & NODE_ELEMENT_FLAGS_TRANSFORM)
        {
            // Add a transform element
            CComPtr<IUnknown> pTransform;
            ThrowFailure(CTransform::Create(IID_PPV_ARGS(&pTransform), this));
            m_Elements.emplace(__uuidof(ITransform), pTransform);
        }
    }
    catch (_com_error &e)
    {
        return e.Error();
    }

    return S_OK;
}

STDMETHODIMP CSceneGraphNode::QueryInterface(REFIID riid, void **ppUnk)
{
    if (riid == __uuidof(ITransform))
    {
        auto it = m_Elements.find(riid);
        if (it == m_Elements.end())
        {
            return E_NOINTERFACE;
        }

        auto pUnk = it->second;
        return pUnk->QueryInterface(riid, ppUnk);
    }

    return __super::QueryInterface(riid, ppUnk);
}

STDMETHODIMP CSceneGraphNode::AddChild(_In_ PCSTR pName, _In_ ISceneGraphNode *pSceneNode)
{
    auto result = m_ChildNodes.emplace(pName, pSceneNode);
    return result.second ? S_OK : E_FAIL;
}


HRESULT CModelInstance::Create(REFIID riid, void **ppModelInstance, CSceneGraphNode *pNode)
{
    try
    {
        ThrowFailure(CreateAggregateElement<CModelInstance>(riid, ppModelInstance, pNode));
    }
    catch (_com_error &e)
    {
        return e.Error();
    }

    return S_OK;
}

HRESULT CCamera::Create(REFIID riid, void **ppCamera, CSceneGraphNode *pNode)
{
    try
    {
        ThrowFailure(CreateAggregateElement<CCamera>(riid, ppCamera, pNode));
    }
    catch (_com_error &e)
    {
        return e.Error();
    }

    return S_OK;
}

HRESULT CLight::Create(REFIID riid, void **ppLight, CSceneGraphNode *pNode)
{
    try
    {
        ThrowFailure(CreateAggregateElement<CLight>(riid, ppLight, pNode));
    }
    catch (_com_error &e)
    {
        return e.Error();
    }

    return S_OK;
}

HRESULT CTransform::Create(REFIID riid, void **ppTransform, CSceneGraphNode *pNode)
{
    try
    {
        ThrowFailure(CreateAggregateElement<CTransform>(riid, ppTransform, pNode));
    }
    catch (_com_error &e)
    {
        return e.Error();
    }

    return S_OK;
}


STDMETHODIMP CSceneGraphIterator::MoveNextSibling()
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

STDMETHODIMP CSceneGraphIterator::Reset(_In_ ISceneGraphNode *pParentNode, _In_opt_ PCSTR pName)
{
    HRESULT hr = S_OK;
    CSceneGraphNode *pParentNodeImpl = reinterpret_cast<CSceneGraphNode *>(pParentNode);
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

STDMETHODIMP CSceneGraphIterator::GetNode(REFIID riid, void **ppNode)
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

STDMETHODIMP CScene::FinalConstruct()
{
    try
    {
        CComPtr<CSceneGraphNode> pRootNode = new CSceneGraphNode(NODE_ELEMENT_FLAGS_TRANSFORM); // throw(std::bad_alloc)
        m_pRootSceneGraphNode = pRootNode;
    }
    catch(std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}
