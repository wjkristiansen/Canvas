//================================================================================================
// CanvasObject
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CCanvasObjectBase :
    public CGenericBase
{
public:
    class CCanvas *m_pCanvas = nullptr;

    CCanvasObjectBase(CCanvas *pCanvas);
    ~CCanvasObjectBase();
};

