//================================================================================================
// NamedObject
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

CObjectName::CObjectName(CNamedObjectList *pNamedObjectList) :
    m_pNamedObjectList(pNamedObjectList)
{
}

CObjectName::~CObjectName()
{
    if (m_pNamedObjectList)
    {
        m_pNamedObjectList->Erase(this);
    }
}

CANVASMETHODIMP CObjectName::SetName(PCSTR szName)
{
    if (m_pNamedObjectList && !m_Name.empty())
    {
        m_pNamedObjectList->Erase(this);
    }

    m_Name = std::string(szName);
    if (!m_Name.empty() && m_pNamedObjectList)
    {
        m_pNamedObjectList->Set(this);
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