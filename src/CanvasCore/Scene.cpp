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

        if (flags & OBJECT_ELEMENT_FLAG_SCENE_GRAPH_NODE)
        {
            pObj->m_Elements.emplace(ISceneGraphNode::IId, std::make_unique<CInnerGeneric<CSceneGraphNode, ISceneGraphNode::IId>>(pObj)); // throw(std::bad_alloc)
        }
        if (flags & OBJECT_ELEMENT_FLAG_TRANSFORM)
        {
            pObj->m_Elements.emplace(ITransform::IId, std::make_unique<CInnerGeneric<CTransform, ITransform::IId>>(pObj)); // throw(std::bad_alloc)
        }
        if (flags & OBJECT_ELEMENT_FLAG_CAMERA)
        {
            pObj->m_Elements.emplace(ICamera::IId, std::make_unique<CInnerGeneric<CCamera, ICamera::IId>>(pObj)); // throw(std::bad_alloc)
        }
        if (flags & OBJECT_ELEMENT_FLAG_LIGHT)
        {
            pObj->m_Elements.emplace(ILight::IId, std::make_unique<CInnerGeneric<CLight, ILight::IId>>(pObj)); // throw(std::bad_alloc)
        }
        if (flags & OBJECT_ELEMENT_FLAG_MODELINSTANCE)
        {
            pObj->m_Elements.emplace(IModelInstance::IId, std::make_unique<CInnerGeneric<CModelInstance, IModelInstance::IId>>(pObj)); // throw(std::bad_alloc)
        }

        return pObj->QueryInterface(iid, ppObj);
    }
    catch(Canvas::Result &res)
    {
        return res;
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
}

//------------------------------------------------------------------------------------------------
CObject::CObject(OBJECT_ELEMENT_FLAGS flags) : // throw(std::bad_alloc)
    CGenericBase()
{
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CObject::InternalQueryInterface(InterfaceId iid, void **ppUnk)
{
    auto it = m_Elements.find(iid);
    if (it != m_Elements.end())
    {
        // Quick path
        return it->second.get()->InternalQueryInterface(iid, ppUnk);
    }
    else
    {
        // Slow path
        // Iterate through the elements and return the first implementer
        for (it = m_Elements.begin(); it != m_Elements.end(); ++it)
        {
            auto res = it->second->InternalQueryInterface(iid, ppUnk);
            if (res == Result::Success)
            {
                return res;
            }
        }
    }

    // Fall through to base implementation
    return __super::InternalQueryInterface(iid, ppUnk);
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
