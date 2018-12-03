//================================================================================================
// Scene
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
Result CObject::Create(OBJECT_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppObj)
{
    try
    {
        CComPtr<CObject> pObj = new CGeneric<CObject>(flags); // throw(std::bad_alloc)

        if (flags & OBJECT_ELEMENT_FLAG_TRANSFORM)
        {
            CComPtr<IGeneric> pTransform;
            ThrowFailure(CObjectElement<CTransform>::Create(ITransform::IId, reinterpret_cast<void **>(&pTransform), pObj));
            pObj->m_Elements.emplace(ITransform::IId, pTransform); // throw(std::bad_alloc)
        }
        if (flags & OBJECT_ELEMENT_FLAG_CAMERA)
        {
            CComPtr<ICamera> pCamera;
            ThrowFailure(CObjectElement<CCamera>::Create(ICamera::IId, reinterpret_cast<void **>(&pCamera), pObj));
            pObj->m_Elements.emplace(ICamera::IId, pCamera); // throw(std::bad_alloc)
        }
        if (flags & OBJECT_ELEMENT_FLAG_LIGHT)
        {
            CComPtr<IGeneric> pLight;
            ThrowFailure(CObjectElement<CLight>::Create(ILight::IId, reinterpret_cast<void **>(&pLight), pObj));
            pObj->m_Elements.emplace(ILight::IId, pLight); // throw(std::bad_alloc)
        }
        if (flags & OBJECT_ELEMENT_FLAG_MODELINSTANCE)
        {
            CComPtr<IGeneric> pModelInstance;
            ThrowFailure(CObjectElement<CModelInstance>::Create(IModelInstance::IId, reinterpret_cast<void **>(&pModelInstance), pObj));
            pObj->m_Elements.emplace(IModelInstance::IId, pModelInstance); // throw(std::bad_alloc)
        }
        return pObj->QueryInterface(iid, ppObj);
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
}

//------------------------------------------------------------------------------------------------
CObject::CObject(OBJECT_ELEMENT_FLAGS flags) : // throw(std::bad_alloc)
    CCanvasObjectBase()
{
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CObject::QueryInterface(InterfaceId iid, void **ppUnk)
{
    auto it = m_Elements.find(iid);
    if (it != m_Elements.end())
    {
        auto pUnk = it->second;
        return pUnk->QueryInterface(iid, ppUnk);
    }

    return CCanvasObjectBase::QueryInterface(iid, ppUnk);
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CSceneGraphNode::Remove()
{
    if(m_pPrevSibling)
    {
        m_pPrevSibling->m_pNextSibling = m_pNextSibling;
    }

    if (m_pNextSibling)
    {
        m_pNextSibling->m_pPrevSibling;
    }

    if (m_pParent)
    {
        if (m_pParent->m_pFirstChild == this)
        {
            m_pParent->m_pFirstChild = m_pNextSibling;
        }

        if (m_pParent->m_pLastChild == this)
        {
            m_pParent->m_pLastChild = m_pPrevSibling;
        }
    }

    m_pParent = nullptr;
    m_pPrevSibling = nullptr;
    m_pNextSibling = nullptr;
    return Result::NotImplemented;
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CSceneGraphNode::Insert(_In_opt_ ISceneGraphNode *pParent, _In_opt_ ISceneGraphNode *pInsertBefore)
{
    CSceneGraphNode *pParentNodeImp = reinterpret_cast<CSceneGraphNode *>(pParent);
    CSceneGraphNode *pInsertBeforeImp = pInsertBefore ? reinterpret_cast<CSceneGraphNode *>(pInsertBefore) : nullptr;
    if (pInsertBeforeImp && pInsertBeforeImp->m_pParent != pParent)
    {
        // Node parameters are not parent-child
        return Result::InvalidArg;
    }

    m_pNextSibling = pInsertBeforeImp;

    if (pInsertBeforeImp)
    {
        m_pPrevSibling = pInsertBeforeImp->m_pPrevSibling;
        pInsertBeforeImp->m_pPrevSibling = this;
        m_pParent = pParentNodeImp;
    }
    else
    {
        // Insert at the end of the list
        m_pPrevSibling = pParentNodeImp->m_pLastChild;
        if (pParentNodeImp->m_pLastChild)
        {
            pParentNodeImp->m_pLastChild->m_pNextSibling = this;
        }
        pParentNodeImp->m_pLastChild = this;
    }

    if (m_pPrevSibling)
    {
        m_pPrevSibling->m_pNextSibling = this;
    }
    else
    {
        pParentNodeImp->m_pFirstChild = this;
    }

    return Result::Success;
}
