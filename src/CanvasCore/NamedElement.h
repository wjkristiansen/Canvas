#pragma once

#include "Gem.hpp"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
template<class _Base>
class TNamedElement :
    public Gem::TGeneric<_Base>
{
    std::string m_Name;

public:
    using BaseType = _Base;

public:
    TNamedElement()
    {
    }

    ~TNamedElement()
    {
    }

    // XCanvasElement methods
    GEMMETHOD_(PCSTR, GetName)() { return m_Name.c_str(); }
    GEMMETHOD_(void, SetName)(PCSTR szName) { m_Name = szName; }
};

}