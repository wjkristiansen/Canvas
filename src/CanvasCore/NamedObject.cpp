//================================================================================================
// NamedObject
//================================================================================================

#include "stdafx.h"

//------------------------------------------------------------------------------------------------
CObjectName::CObjectName(XGeneric *pOuterGeneric, PCSTR szName, CCanvas *pCanvas) :
    CInnerGenericBase(pOuterGeneric),
    m_Name(szName ? szName : ""),
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
CANVASMETHODIMP CObjectName::SetName(PCSTR szName)
{
    CCanvasObjectBase *pCanvasObject = reinterpret_cast<CCanvasObjectBase *>(m_pOuterGeneric);
    CCanvas *pCanvas = pCanvasObject->m_pCanvas;

    // Erase old entry
    if (!m_Name.empty())
    {
        pCanvas->m_ObjectNames.erase(m_Name);
    }

    m_Name = std::string(szName);

    // Add new entry
    if (!m_Name.empty())
    {
        pCanvas->m_ObjectNames.emplace(m_Name, this);
    }

    return Result::Success;
}

