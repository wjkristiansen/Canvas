//================================================================================================
// CanvasElement
//================================================================================================

#pragma once

#include "Gem.hpp"
#include "Canvas.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
template<class _Base>
class TCanvasElement :
    public Gem::TGeneric<_Base>
{
    std::string m_Name;
    CCanvas *m_pCanvas;

public:
    using BaseType = _Base;

public:
    TCanvasElement(CCanvas *pCanvas) :
        m_pCanvas(pCanvas)
    {
    }

    ~TCanvasElement()
    {
        m_pCanvas->CanvasElementDestroyed(this);
    }

    // XCanvasElement methods
    GEMMETHOD_(PCSTR, GetName)() { return m_Name.c_str(); }
    GEMMETHOD_(void, SetName)(PCSTR szName) { m_Name = szName; }
    GEMMETHOD_(XCanvas *, GetCanvas()) { return m_pCanvas; }
    GEMMETHOD_(PCSTR, GetTypeName)() { return _Base::XFaceName; }
};

}