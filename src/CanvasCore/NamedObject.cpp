//================================================================================================
// Name
//================================================================================================

#include "stdafx.h"

//------------------------------------------------------------------------------------------------
CName::CName(XGeneric *pOuterGeneric, PCWSTR szName, CCanvas *pCanvas) :
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
CName::~CName()
{
    if (!m_Name.empty())
    {
        m_pCanvas->m_ObjectNames.erase(m_Name);
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CName::SetName(PCWSTR szName)
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
        m_pCanvas->Logger().LogErrorF(L"Out of memory: CName::SetName: szName = %s", szName);
        return Result::OutOfMemory;
    }

    return Result::Success;
}

