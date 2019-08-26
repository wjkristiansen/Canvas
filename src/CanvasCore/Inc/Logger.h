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

    void Log(QLog::Category LogCategory, PCSTR szOutput)
    {
        if (m_pLogClient)
        {
            if (m_pLogClient->LogEntryBegin(LogCategory, "CANVAS", szOutput))
            {
                m_pLogClient->LogEntryEnd();
            }
        }
    }

    void Log(QLog::Category LogCategory, PCSTR szFormat, va_list args)
    {
        if (m_pLogClient)
        {
            if (m_pLogClient->LogEntryBeginVA(LogCategory, "CANVAS", szFormat, args))
            {
                m_pLogClient->LogEntryEnd();
            }
        }
    }

    void LogF(QLog::Category LogCategory, PCSTR szFormat, ...)
    {
        va_list args;
        va_start(args, szFormat);
        Log(LogCategory, szFormat, args);
        va_end(args);
    }

    void LogCritical(PCSTR szOutput)
    {
        Log(QLog::Category::Critical, szOutput);
    }

    void LogCriticalF(PCSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        Log(QLog::Category::Critical, szOutput, args);
        va_end(args);
    }

    void LogError(PCSTR szOutput)
    {
        Log(QLog::Category::Error, szOutput);
    }

    void LogErrorF(PCSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        Log(QLog::Category::Error, szOutput, args);
        va_end(args);
    }

    void LogWarning(PCSTR szOutput)
    {
        Log(QLog::Category::Warning, szOutput);
    }

    void LogWarningF(PCSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        Log(QLog::Category::Warning, szOutput, args);
        va_end(args);
    }

    void LogInfo(PCSTR szOutput)
    {
        Log(QLog::Category::Info, szOutput);
    }

    void LogInfoF(PCSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        Log(QLog::Category::Info, szOutput, args);
        va_end(args);
    }

    void LogDebug(PCSTR szFile, UINT LineNumber, PCSTR szOutput)
    {
        Log(QLog::Category::Debug, szOutput);
    }

    void LogDebugF(PCSTR szFile, UINT LineNumber, PCSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        Log(QLog::Category::Debug, szOutput, args);
        va_end(args);
    }
};
