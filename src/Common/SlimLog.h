//================================================================================================
// SlimLog
//================================================================================================

#pragma once

namespace SlimLog
{
    enum LOG_CATEGORY
    {
        LOG_CATEGORY_ERROR = 0x01,
        LOG_CATEGORY_WARNING = 0x02,
        LOG_CATEGORY_MESSAGE = 0x04,
        LOG_CATEGORY_INFO = 0x08,
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
        long m_CategoryMask = LOG_CATEGORY_ERROR | LOG_CATEGORY_WARNING | LOG_CATEGORY_MESSAGE;

    public:
        TLogger(_LogOutputClass *pOutput) :
            m_pOutput(pOutput) {}

        int SetCategoryMask(long FilterMask)
        {
            return InterlockedExchange(&m_CategoryMask, FilterMask);
        }

        int GetCategoryMask() const { return m_CategoryMask; }

        // Write to log output
        template<LOG_CATEGORY Category>
        void LogOutput(PCWSTR szPrefix, PCWSTR szOutputString)
        {
            if (m_pOutput && 0 != (m_CategoryMask & Category))
            {
                m_pOutput->Output(szPrefix, szOutputString);
            }
        }

        // Write to log output
        template<LOG_CATEGORY Category>
        void LogOutputVA(PCWSTR szPrefix, PCWSTR szFormat, va_list args)
        {
            if (m_pOutput && 0 != (m_CategoryMask & Category))
            {
                vswprintf_s(m_szBuffer, szFormat, args);
                m_pOutput->Output(szPrefix, m_szBuffer);
            }
        }
    };
}