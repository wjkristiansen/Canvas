//================================================================================================
// Object
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
inline CCanvasObjectBase::CCanvasObjectBase(CCanvas *pCanvas) :
    CGenericBase(),
    m_pCanvas(pCanvas),
    m_OutstandingNode(pCanvas->m_OutstandingObjects.GetLast(), this)
{
}
