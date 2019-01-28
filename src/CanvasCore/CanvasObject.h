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

    CCanvasObjectBase(class CCanvas *pCanvas);
    ~CCanvasObjectBase();
};

