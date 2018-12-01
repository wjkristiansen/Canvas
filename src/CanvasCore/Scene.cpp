//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

CSceneGraphNode::CSceneGraphNode(NODE_ELEMENT_FLAGS flags) : // throw(std::bad_alloc)
    CCanvasObjectBase()
{
}


Result CSceneGraphNode::Create(NODE_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppObj)
{
    try
    {
        CComPtr<CSceneGraphNode> pNode = new CGeneric<CSceneGraphNode>(flags); // throw(std::bad_alloc)
        if (flags & NODE_ELEMENT_FLAGS_TRANSFORM)
        {
            CComPtr<IGeneric> pTransform;
            ThrowFailure(CSceneNodeElement<CTransform>::Create(ITransform::IId, reinterpret_cast<void **>(&pTransform), pNode));
            pNode->m_Elements.emplace(ITransform::IId, pTransform); // throw(std::bad_alloc)
        }
        if (flags & NODE_ELEMENT_FLAGS_CAMERA)
        {
            CComPtr<ICamera> pCamera;
            ThrowFailure(CSceneNodeElement<CCamera>::Create(ICamera::IId, reinterpret_cast<void **>(&pCamera), pNode));
            pNode->m_Elements.emplace(ICamera::IId, pCamera); // throw(std::bad_alloc)
        }
        if (flags & NODE_ELEMENT_FLAGS_LIGHT)
        {
            CComPtr<IGeneric> pLight;
            ThrowFailure(CSceneNodeElement<CLight>::Create(ILight::IId, reinterpret_cast<void **>(&pLight), pNode));
            pNode->m_Elements.emplace(ILight::IId, pLight); // throw(std::bad_alloc)
        }
        if (flags & NODE_ELEMENT_FLAGS_MODELINSTANCE)
        {
            CComPtr<IGeneric> pModelInstance;
            ThrowFailure(CSceneNodeElement<CModelInstance>::Create(IModelInstance::IId, reinterpret_cast<void **>(&pModelInstance), pNode));
            pNode->m_Elements.emplace(IModelInstance::IId, pModelInstance); // throw(std::bad_alloc)
        }
        return pNode->QueryInterface(iid, ppObj);
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
}

CANVASMETHODIMP CSceneGraphNode::QueryInterface(InterfaceId iid, void **ppUnk)
{
    auto it = m_Elements.find(iid);
    if (it != m_Elements.end())
    {
        auto pUnk = it->second;
        return pUnk->QueryInterface(iid, ppUnk);
    }

    return CCanvasObjectBase::QueryInterface(iid, ppUnk);
}

CANVASMETHODIMP CSceneGraphNode::AddChild(_In_ PCSTR pName, _In_ ISceneGraphNode *pSceneNode)
{
    auto result = m_ChildNodes.emplace(pName, pSceneNode);
    return result.second ? Result::Success : Result::DuplicateKey;
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
