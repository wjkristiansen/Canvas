//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

CSceneGraphNode::CSceneGraphNode(NODE_ELEMENT_FLAGS flags) : // throw(std::bad_alloc)
    CGeneric<CSceneGraphNodeBase>()
{
    if (flags & NODE_ELEMENT_FLAGS_TRANSFORM)
    {
        CComPtr<IGeneric> pTransform;
        ThrowFailure(CTransform::Create(ITransform::IId, reinterpret_cast<void **>(&pTransform), this));
        m_Elements.emplace(ITransform::IId, pTransform); // throw(std::bad_alloc)
    }
    if (flags & NODE_ELEMENT_FLAGS_CAMERA)
    {
        CComPtr<ICamera> pCamera;
        ThrowFailure(CCamera::Create(ICamera::IId, reinterpret_cast<void **>(&pCamera), this));
        m_Elements.emplace(ICamera::IId, pCamera); // throw(std::bad_alloc)
    }
    if (flags & NODE_ELEMENT_FLAGS_LIGHT)
    {
        CComPtr<IGeneric> pLight;
        ThrowFailure(CLight::Create(ILight::IId, reinterpret_cast<void **>(&pLight), this));
        m_Elements.emplace(ILight::IId, pLight); // throw(std::bad_alloc)
    }
    if (flags & NODE_ELEMENT_FLAGS_MODELINSTANCE)
    {
        CComPtr<IGeneric> pModelInstance;
        ThrowFailure(CModelInstance::Create(IModelInstance::IId, reinterpret_cast<void **>(&pModelInstance), this));
        m_Elements.emplace(IModelInstance::IId, pModelInstance); // throw(std::bad_alloc)
    }
}


Result CSceneGraphNode::Create(NODE_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppObj)
{
    try
    {
        CComPtr<CSceneGraphNode> pNode = new CSceneGraphNode(flags); // throw(std::bad_alloc)
        return pNode->QueryInterface(iid, ppObj);
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
}

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
    if (!ppModelInstance)
    {
        return Result::BadPointer;
    }

    *ppModelInstance = nullptr;

    try
    {
        CComPtr<CModelInstance> pModelInstance = new CGeneric<CModelInstance>(pNode); // throw(std::bad_alloc)
        return pModelInstance->QueryInterface(iid, ppModelInstance);
    }
    catch (CanvasError &e)
    {
        return e.Result();
    }

    return Result::Success;
}

Result CCamera::Create(InterfaceId iid, void **ppCamera, CSceneGraphNode *pNode)
{
    if (!ppCamera)
    {
        return Result::BadPointer;
    }

    *ppCamera = nullptr;

    try
    {
        CComPtr<CCamera> pCamera = new CGeneric<CCamera>(pNode); // throw(std::bad_alloc)
        return pCamera->QueryInterface(iid, ppCamera);
    }
    catch (CanvasError &e)
    {
        return e.Result();
    }

    return Result::Success;
}

Result CLight::Create(InterfaceId iid, void **ppLight, CSceneGraphNode *pNode)
{
    if (!ppLight)
    {
        return Result::BadPointer;
    }

    *ppLight = nullptr;

    try
    {
        CComPtr<CLight> pLight = new CGeneric<CLight>(pNode); // throw(std::bad_alloc)
        return pLight->QueryInterface(iid, ppLight);
    }
    catch (CanvasError &e)
    {
        return e.Result();
    }

    return Result::Success;
}

Result CTransform::Create(InterfaceId iid, void **ppTransform, CSceneGraphNode *pNode)
{
    if (!ppTransform)
    {
        return Result::BadPointer;
    }

    *ppTransform = nullptr;
    try
    {
        CComPtr<CTransform> pTransform = new CGeneric<CTransform>(pNode); // throw(std::bad_alloc)
        return pTransform->QueryInterface(iid, ppTransform);
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
