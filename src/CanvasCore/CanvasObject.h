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

    GEMMETHOD_(ObjectType, GetType)() const = 0;

    CCanvasObjectBase(CCanvas *pCanvas);
    ~CCanvasObjectBase();
};

//------------------------------------------------------------------------------------------------
template<ObjectType _Type>
class TCanvasObject
{
    ObjectType GetType() const override { return _Type; }
};

