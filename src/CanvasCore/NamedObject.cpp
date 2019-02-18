//================================================================================================
// NamedObject
//================================================================================================

#include "stdafx.h"

//------------------------------------------------------------------------------------------------
CObjectName::CObjectName(XGeneric *pOuterGeneric, PCWSTR szName, CCanvas *pCanvas) :
    CInnerGenericBase(pOuterGeneric),
    m_Name(szName ? szName : L""),
    m_pCanvas(pCanvas)
{
    if (!m_Name.empty())
    {
        pCanvas->m_ObjectNames.emplace(m_Name, this);
    }
}

//------------------------------------------------------------------------------------------------
CObjectName::~CObjectName()
{
    if (!m_Name.empty())
    {
        m_pCanvas->m_ObjectNames.erase(m_Name);
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CObjectName::SetName(PCWSTR szName)
{
    try
    {
        // Erase old entry
        if (!m_Name.empty())
        {
            m_pCanvas->m_ObjectNames.erase(m_Name);
        }

        m_Name = std::wstring(szName); // throw(std::bad_alloc)

        // Add new entry
        if (!m_Name.empty())
        {
            m_pCanvas->m_ObjectNames.emplace(m_Name, this); // throw(std::bad_alloc)
        }
    }
    catch (std::bad_alloc &)
    {
        m_pCanvas->Logger().LogErrorF(L"Out of memory: CObjectName::SetName: szName = %s", szName);
        return Result::OutOfMemory;
    }

    return Result::Success;
}

