//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//CANVASMETHODIMP CSceneGraphNode::FinalConstruct()
//{
//    try
//    {
//        ThrowFailure(__super::FinalConstruct());
//
//        if (m_NodeElementFlags & NODE_ELEMENT_FLAGS_TRANSFORM)
//        {
//            // Add a transform element
//            CComPtr<IGeneric> pTransform;
//            ThrowFailure(CTransform::Create(IID_PPV_ARGS(&pTransform), this));
//            m_Elements.emplace(__uuidof(ITransform), pTransform);
//        }
//    }
//    catch (CanvasError &e)
//    {
//        return e.Result();
//    }
//
//    return Result::Success;
//}

CANVASMETHODIMP CSceneGraphNode::QueryInterface(InterfaceId iid, void **ppUnk)
{
    if (iid == ITransform::IId)
    {
        auto it = m_Elements.find(iid);
        if (it == m_Elements.end())
        {
            return Result::NoInterface;
        }

        auto pUnk = it->second;
        return pUnk->QueryInterface(iid, ppUnk);
    }

    return __super::QueryInterface(iid, ppUnk);
}

CANVASMETHODIMP CSceneGraphNode::AddChild(_In_ PCSTR pName, _In_ ISceneGraphNode *pSceneNode)
{
    auto result = m_ChildNodes.emplace(pName, pSceneNode);
    return result.second ? Result::Success : Result::DuplicateKey;
}


Result CModelInstance::Create(InterfaceId iid, void **ppModelInstance, CSceneGraphNode *pNode)
{
    try
    {
//        ThrowFailure(CreateAggregateElement<CModelInstance>(iid, ppModelInstance, pNode));
    }
    catch (CanvasError &e)
    {
        return e.Result();
    }

    return Result::Success;
}

Result CCamera::Create(InterfaceId iid, void **ppCamera, CSceneGraphNode *pNode)
{
    try
    {
//        ThrowFailure(CreateAggregateElement<CCamera>(iid, ppCamera, pNode));
    }
    catch (CanvasError &e)
    {
        return e.Result();
    }

    return Result::Success;
}

Result CLight::Create(InterfaceId iid, void **ppLight, CSceneGraphNode *pNode)
{
    try
    {
//        ThrowFailure(CreateAggregateElement<CLight>(iid, ppLight, pNode));
    }
    catch (CanvasError &e)
    {
        return e.Result();
    }

    return Result::Success;
}

Result CTransform::Create(InterfaceId iid, void **ppTransform, CSceneGraphNode *pNode)
{
    try
    {
//        ThrowFailure(CreateAggregateElement<CTransform>(iid, ppTransform, pNode));
    }
    catch (CanvasError &e)
    {
        return e.Result();
    }

    return Result::Success;
}


CANVASMETHODIMP CSceneGraphIterator::MoveNextSibling()
{
    if (m_It != m_pContainingSceneGraphNode->m_ChildNodes.end())
    {
        ++m_It;
        if (m_It != m_pContainingSceneGraphNode->m_ChildNodes.end())
        {
            return Result::Success;
        }
    }

    return Result::Finished;
}

CANVASMETHODIMP CSceneGraphIterator::Reset(_In_ ISceneGraphNode *pParentNode, _In_opt_ PCSTR pName)
{
    Result result = Result::Success;
    CSceneGraphNode *pParentNodeImpl = reinterpret_cast<CSceneGraphNode *>(pParentNode);
    CSceneGraphNode::NodeMapType::iterator it;
    if(pName)
    {
        it = pParentNodeImpl->m_ChildNodes.find(pName);
        if(it == pParentNodeImpl->m_ChildNodes.end())
        {
            result = Result::NotFound;
        }
    }
    else
    {
        it = pParentNodeImpl->m_ChildNodes.begin();
    }

    m_It = it;

    m_pContainingSceneGraphNode = pParentNodeImpl;

    return result;
}

CANVASMETHODIMP CSceneGraphIterator::GetNode(InterfaceId iid, void **ppNode)
{
    if(m_pContainingSceneGraphNode)
    {
        return m_pContainingSceneGraphNode->QueryInterface(iid, ppNode);
    }
    else
    {
        return Result::Uninitialized;
    }
}
//
//CANVASMETHODIMP CScene::FinalConstruct()
//{
//    try
//    {
//        CComPtr<CSceneGraphNode> pRootNode = new CSceneGraphNode(NODE_ELEMENT_FLAGS_TRANSFORM); // throw(std::bad_alloc)
//        m_pRootSceneGraphNode = pRootNode;
//    }
//    catch(std::bad_alloc&)
//    {
//        return E_OUTOFMEMORY;
//    }
//
//    return Result::Success;
//}
