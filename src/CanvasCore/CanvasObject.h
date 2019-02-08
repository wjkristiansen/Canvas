//================================================================================================
// CanvasObject
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CCanvasObjectBase :
    public CGenericBase
{
public:
    TAutoListNode<TStaticPtr<CCanvasObjectBase>> m_OutstandingNode;

    CCanvasObjectBase(class CCanvas *pCanvas);
};