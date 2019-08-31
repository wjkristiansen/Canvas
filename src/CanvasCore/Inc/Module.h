//================================================================================================
// Module
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CModule
{
    HMODULE m_hModule = NULL;

public:
    CModule() = default;
    explicit CModule(HMODULE hModule) :
        m_hModule(hModule) {}
    CModule(CModule &&o) noexcept :
        m_hModule(std::move(o.m_hModule))
    {
        o.m_hModule = NULL;
    }
    CModule(const CModule &o) = delete;
    ~CModule()
    {
        if (m_hModule)
        {
            FreeLibrary(m_hModule);
        }
    }

    CModule &operator=(CModule &&o) noexcept
    {
        m_hModule = o.m_hModule;
        o.m_hModule = NULL;
        return *this;
    }

    CModule &operator=(const CModule &o) = delete;

    HMODULE Get() const { return m_hModule; }
};
