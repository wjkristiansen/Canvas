#pragma once

class CScopedHandle
{
    HANDLE m_Handle = NULL;

public:
    CScopedHandle() = default;
    CScopedHandle(HANDLE Handle) noexcept :
        m_Handle(Handle) {}
    CScopedHandle(const CScopedHandle &) = delete;
    CScopedHandle(CScopedHandle &&o) noexcept :
        m_Handle(o.m_Handle)
    {
        o.m_Handle = NULL;
    }
    ~CScopedHandle()
    {
        if (m_Handle)
        {
            CloseHandle(m_Handle);
        }
    }

    void Close()
    {
        if (m_Handle)
        {
            CloseHandle(m_Handle);
            m_Handle = NULL;
        }
    }

    HANDLE Detach()
    {
        HANDLE Handle = m_Handle;
        m_Handle = NULL;
        return Handle;
    }

    CScopedHandle &operator=(HANDLE Handle)
    {
        if (m_Handle)
        {
            CloseHandle(m_Handle);
        }
        m_Handle = Handle;
        return *this;
    }
    CScopedHandle &operator=(const CScopedHandle &) = delete;
    CScopedHandle &operator=(CScopedHandle &&o) noexcept
    {
        if (m_Handle)
        {
            CloseHandle(m_Handle);
        }
        m_Handle = o.m_Handle;
        o.m_Handle = NULL;
        return *this;
    }

    operator HANDLE() const
    {
        return m_Handle;
    }

    HANDLE Get() const
    {
        return m_Handle;
    }
};

