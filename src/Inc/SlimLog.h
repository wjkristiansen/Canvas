//================================================================================================
// SlimLog
//================================================================================================

#pragma once

namespace SlimLog
{
    enum LOG_Group
    {
        LOG_Group_ERROR = 0x01,
        LOG_Group_WARNING = 0x02,
        LOG_Group_MESSAGE = 0x04,
        LOG_Group_INFO = 0x08,
    };

    //------------------------------------------------------------------------------------------------
    class CLogOutputBase
    {
    public:
        virtual void Output(PCWSTR szHeader, PCWSTR szString) = 0;
    };

    //------------------------------------------------------------------------------------------------
    template<class _LogOutputClass>
    class TLogger
    {
        static inline __declspec(thread) WCHAR m_szBuffer[4096];
        _LogOutputClass *m_pOutput = nullptr;
        long m_GroupMask = LOG_Group_ERROR | LOG_Group_WARNING | LOG_Group_MESSAGE;

    public:
        TLogger(_LogOutputClass *pOutput) :
            m_pOutput(pOutput) {}

        int SetGroupMask(long FilterMask)
        {
            return InterlockedExchange(&m_GroupMask, FilterMask);
        }

        int GetGroupMask() const { return m_GroupMask; }

        // Write to log output
        template<LOG_Group Group>
        void LogOutput(PCWSTR szPrefix, PCWSTR szOutputString)
        {
            if (m_pOutput && 0 != (m_GroupMask & Group))
            {
                m_pOutput->Output(szPrefix, szOutputString);
            }
        }

        // Write to log output
        template<LOG_Group Group>
        void LogOutputVA(PCWSTR szPrefix, PCWSTR szFormat, va_list args)
        {
            if (m_pOutput && 0 != (m_GroupMask & Group))
            {
                vswprintf_s(m_szBuffer, szFormat, args);
                m_pOutput->Output(szPrefix, m_szBuffer);
            }
        }
    };
}