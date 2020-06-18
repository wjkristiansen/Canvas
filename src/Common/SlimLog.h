//================================================================================================
// SlimLog
//================================================================================================

#pragma once

namespace SlimLog
{
    enum LOG_LEVEL
    {
        LOG_LEVEL_ERROR = 0x01,
        LOG_LEVEL_WARNING = 0x02,
        LOG_LEVEL_MESSAGE = 0x04,
        LOG_LEVEL_INFO = 0x08,
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
        long m_OutputMask = LOG_LEVEL_ERROR | LOG_LEVEL_WARNING | LOG_LEVEL_MESSAGE;

    public:
        TLogger(_LogOutputClass *pOutput) :
            m_pOutput(pOutput) {}

        int SetOutputMask(long FilterMask)
        {
            return InterlockedExchange(&m_OutputMask, FilterMask);
        }

        int GetOutputMask() const { return m_OutputMask; }

        template<LOG_LEVEL Level>
        void LogOutput(PCWSTR szPrefix, PCWSTR szOutputString)
        {
            if (0 != (m_OutputMask & Level) && m_pOutput)
            {
                m_pOutput->Output(szPrefix, szOutputString);
            }
        }

        template<LOG_LEVEL Level>
        void LogOutputVA(PCWSTR szPrefix, PCWSTR szFormat, va_list args)
        {
            if (0 != (m_OutputMask & Level) && m_pOutput)
            {
                vswprintf_s(m_szBuffer, szFormat, args);
                m_pOutput->Output(szPrefix, m_szBuffer);
            }
        }
    };
}