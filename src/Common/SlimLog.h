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

    class CBasicLogOutput
    {
        wchar_t m_Buffer[4096]; // Not thread-safe

    public:
        CBasicLogOutput() = default;

        void BeginOutput(PCWSTR szHeader)
        {
            OutputString(szHeader);
            OutputString(L": ");
        }

        void OutputString(PCWSTR szString)
        {
            // Debugger
            OutputDebugStringW(szString);

            // Console
            wprintf_s(szString);
        }

        void OutputStringVA(PCWSTR szFormat, va_list args)
        {
            // Build output string in buffer (not thread-safe)
            vswprintf_s(m_Buffer, szFormat, args);

            OutputString(m_Buffer);
        }

        void EndOutput()
        {
            OutputString(L"\n");
        }
    };

    template<class _LogOutputClass = CBasicLogOutput>
    class TLogger
    {
        int m_FilterMask = LOG_LEVEL_ERROR | LOG_LEVEL_WARNING | LOG_LEVEL_MESSAGE;
        _LogOutputClass m_Output;

    public:
        TLogger() = default;

        bool BeginOutput(LOG_LEVEL Level, PCWSTR szPrefix)
        {
            if (m_FilterMask & Level)
            {
                m_Output.BeginOutput(szPrefix);
                return true;
            }

            return false;
        }

        void OutputString(PCWSTR szOutputString)
        {
            m_Output.OutputString(szOutputString);
        }

        void OutputStringVA(PCWSTR szFormat, va_list args)
        {
            m_Output.OutputStringVA(szFormat, args);
        }

        void EndOutput()
        {
            m_Output.EndOutput();
        }

        template<LOG_LEVEL Level>
        void LogWrite(PCWSTR szPrefix, PCWSTR szOutputString)
        {
            if (BeginOutput(Level, szPrefix))
            {
                OutputString(szOutputString);
                EndOutput();
            }
        }

        template<LOG_LEVEL Level>
        void LogWriteF(PCWSTR szPrefix, PCWSTR szFormat, ...)
        {
            if (LogBeginOutput(Level, szPrefix))
            {
                va_list args;
                va_start(args, szFormat);
                OutputStringVA(szFormat, args);
                va_end(args);
                EndOutput();
            }
        }
    };
}