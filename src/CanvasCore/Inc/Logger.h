//================================================================================================
// SlimLog
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CCanvasLogger
{
    QLog::CLogClient *m_pLogClient;

public:
    CCanvasLogger(QLog::CLogClient *pLogClient) :
        m_pLogClient(pLogClient) {}

    void Log(QLog::Category LogCategory, PCWSTR szOutput)
    {
        if (m_pLogClient)
        {
            if (m_pLogClient->LogEntryBegin(LogCategory, L"CANVAS", szOutput))
            {
                m_pLogClient->LogEntryEnd();
            }
        }
    }

    void Log(QLog::Category LogCategory, PCWSTR szFormat, va_list args)
    {
        if (m_pLogClient)
        {
            if (m_pLogClient->LogEntryBeginVA(LogCategory, L"CANVAS", szFormat, args))
            {
                m_pLogClient->LogEntryEnd();
            }
        }
    }

    void LogF(QLog::Category LogCategory, PCWSTR szFormat, ...)
    {
        va_list args;
        va_start(args, szFormat);
        Log(LogCategory, szFormat, args);
        va_end(args);
    }

    void LogCritical(PCWSTR szOutput)
    {
        Log(QLog::Category::Critical, szOutput);
    }

    void LogCriticalF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        Log(QLog::Category::Critical, szOutput, args);
        va_end(args);
    }

    void LogError(PCWSTR szOutput)
    {
        Log(QLog::Category::Error, szOutput);
    }

    void LogErrorF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        Log(QLog::Category::Error, szOutput, args);
        va_end(args);
    }

    void LogWarning(PCWSTR szOutput)
    {
        Log(QLog::Category::Warning, szOutput);
    }

    void LogWarningF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        Log(QLog::Category::Warning, szOutput, args);
        va_end(args);
    }

    void LogInfo(PCWSTR szOutput)
    {
        Log(QLog::Category::Info, szOutput);
    }

    void LogInfoF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        Log(QLog::Category::Info, szOutput, args);
        va_end(args);
    }

    void LogDebug(PCWSTR szFile, UINT LineNumber, PCWSTR szOutput)
    {
        Log(QLog::Category::Debug, szOutput);
    }

    void LogDebugF(PCWSTR szFile, UINT LineNumber, PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        Log(QLog::Category::Debug, szOutput, args);
        va_end(args);
    }
};
