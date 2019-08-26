//================================================================================================
// Name
//================================================================================================

#include "stdafx.h"

//------------------------------------------------------------------------------------------------
CNameTag::CNameTag(XGeneric *pOuterGeneric, CCanvas *pCanvas, PCSTR szName) :
    CInnerGenericBase(pOuterGeneric),
    m_pCanvas(pCanvas)
{
    if (nullptr != szName)
    {
        SetName(szName);
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(PCSTR) CNameTag::GetName()
{
    return m_Tag.IsAssigned() ? m_Tag.GetKey().c_str() : nullptr;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CNameTag::SetName(PCSTR szName)
{
    Gem::Result result = Gem::Result::Success;

    if (szName)
    {
        try
        {
            m_Tag.Assign(szName, m_pOuterGeneric, &m_pCanvas->m_ObjectNames); // throw(std::bad_alloc)
        }
        catch(std::bad_alloc &)
        {
            result = Gem::Result::OutOfMemory;
        }
        catch (std::exception &)
        {
            result = Gem::Result::InvalidArg; // Consider adding Result::Duplicate
        }
    }
    else
    {
        m_Tag.Unassign();
    }

    return result;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CNameTag::InternalQueryInterface(InterfaceId iid, _Outptr_ void **ppObj)
{
    if (XNameTag::IId == iid)
    {
        *ppObj = this;
        AddRef();
        return Result::Success;
    }

    return __super::InternalQueryInterface(iid, ppObj);
}
