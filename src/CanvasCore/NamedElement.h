#pragma once

#include "Gem.hpp"
#include <optional>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
template<class _Base>
class TNamedElement :
    public Gem::TGeneric<_Base>
{
    std::optional<std::string> m_Name;

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
    GEMMETHOD_(PCSTR, GetName)() { return m_Name.has_value() ? m_Name->c_str() : nullptr; }
    GEMMETHOD_(void, SetName)(PCSTR szName) { m_Name = szName; }
};

}