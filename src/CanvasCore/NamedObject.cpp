//================================================================================================
// NamedObject
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

CANVASMETHODIMP CNamedObjectList::Select(PCSTR szName, InterfaceId iid, void **ppObj)
{
    auto it = m_NamedObjectMap.find(szName);
    if (it != m_NamedObjectMap.end())
    {
        return it->second->QueryInterface(iid, ppObj);
    }

    return Result::NotFound;
}