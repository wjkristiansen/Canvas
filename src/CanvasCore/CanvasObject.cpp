//================================================================================================
// CanvasObject
//================================================================================================

#include "stdafx.h"

//------------------------------------------------------------------------------------------------
CCanvasObjectBase::CCanvasObjectBase(CCanvas *pCanvas) :
    m_pCanvas(pCanvas)
{
    try
    {
        pCanvas->m_OutstandingObjects.insert(this); // throw(std::bad_alloc)
    }
    catch (std::bad_alloc &)
    {
        // Drop tracking of object...
    }
}

//------------------------------------------------------------------------------------------------
CCanvasObjectBase::~CCanvasObjectBase()
{
    m_pCanvas->m_OutstandingObjects.erase(this);
}
