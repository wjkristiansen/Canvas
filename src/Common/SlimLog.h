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

    template<class _LogBeginOutputFunc, class _LogOutputFunc, class _LogEndOutputFunc>
    class TLogger
    {
        int m_FilterMask = LOG_LEVEL_ERROR | LOG_LEVEL_WARNING | LOG_LEVEL_MESSAGE;

    public:
        bool LogBeginOutput(LOG_LEVEL Level, PCWSTR szPrefix)
        {
            if (m_FilterMask & Level)
            {
                _LogBeginOutputFunc(szPrefix);
            }
        }

        void LogOutputString(PCWSTR szOutputString)
        {
            _LogOutputFunc(szOutputString);
        }

        void LogEndOutput()
        {
            _LogEndOutputFunc();
        }

        template<LOG_LEVEL Level>
        void LogWrite(PCWSTR szPrefix, PCWSTR szLogText)
        {
            if (LogBeginOutput(Level, szPrefix))
            {
                LogOutput(szLogText);
                LogEndOutput();
            }
        }

        template<LOG_LEVEL Level>
        void LogWriteF(PCWSTR szPrefix, PCWSTR szFormat, ...)
        {
            if (LogBeginOutput(Level, szPrefix))
            {
                va_list args;
                va_start(args, szFormat);
                vprintf_s(szFormat, args);
                va_end(args);
            }
        }

        void LogError(PCWSTR szPrefix, PCWSTR szLogText)
        {
            LogWrite<LOG_LEVEL_ERROR>(szPrefix, szLogText);
        }

        void LogWarning(PCWSTR szPrefix, PCWSTR szLogText)
        {
            LogWrite<LOG_LEVEL_WARNING>(szPrefix, szLogText);
        }

        void LogMessage(PCWSTR szPrefix, PCWSTR szLogText)
        {
            LogWrite<LOG_LEVEL_MESSAGE>(szPrefix, szLogText);
        }

        void LogInfo(PCWSTR szPrefix, PCWSTR szLogText)
        {
            LogWrite<LOG_LEVEL_INFO>(szPrefix, szLogText);
        }
    };
}