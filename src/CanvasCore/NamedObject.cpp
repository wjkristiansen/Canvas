//================================================================================================
// NamedObject
//================================================================================================

#include "stdafx.h"

CObjectName::CObjectName(XGeneric *pOuterGeneric) :
    CInnerGenericBase(pOuterGeneric)
{
}

CObjectName::~CObjectName()
{
    CCanvasObjectBase *pCanvasObject = reinterpret_cast<CCanvasObjectBase *>(m_pOuterGeneric);
    CCanvas *pCanvas = pCanvasObject->m_pCanvas;
    if (!m_Name.empty())
    {
        pCanvas->m_NamedObjects.Erase(this);
    }
}

CANVASMETHODIMP CObjectName::SetName(PCSTR szName)
{
    CCanvasObjectBase *pCanvasObject = reinterpret_cast<CCanvasObjectBase *>(m_pOuterGeneric);
    CCanvas *pCanvas = pCanvasObject->m_pCanvas;

    if (!m_Name.empty())
    {
        pCanvas->m_NamedObjects.Erase(this);
    }

    m_Name = std::string(szName);
    if (!m_Name.empty())
    {
        pCanvas->m_NamedObjects.Set(this);
    }

    return Result::Success;
}

CANVASMETHODIMP CNamedObjectList::Select(PCSTR szName, InterfaceId iid, void **ppObj)
{
    auto it = m_ObjectNames.find(szName);
    if (it != m_ObjectNames.end())
    {
        return it->second->QueryInterface(iid, ppObj);
    }

    return Result::NotFound;
}