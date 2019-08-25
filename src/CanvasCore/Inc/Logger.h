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

    void LogError(PCWSTR szOutput)
    {
        if (m_pLogClient)
        {
            if (m_pLogClient->LogEntryBegin(QLog::Category::Error, L"CANVAS", szOutput))
            {
                m_pLogClient->LogEntryEnd();
            }
        }
    }

    void LogErrorF(PCWSTR szOutput, ...);
    //{
    //    va_list args;
    //    va_start(args, szOutput);
    //    LogOutputVA<SlimLog::Error>(L"CANVAS ERROR", szOutput, args);
    //    va_end(args);
    //}

    void LogWarning(PCWSTR szOutput)
    {
        if (m_pLogClient)
        {
            if (m_pLogClient->LogEntryBegin(QLog::Category::Warning, L"CANVAS", szOutput))
            {
                m_pLogClient->LogEntryEnd();
            }
        }
    }

    void LogWarningF(PCWSTR szOutput, ...);
    //{
    //    va_list args;
    //    va_start(args, szOutput);
    //    LogOutputVA<SlimLog::Warning>(L"CANVAS WARNING", szOutput, args);
    //    va_end(args);
    //}

    void LogInfo(PCWSTR szOutput)
    {
        if (m_pLogClient)
        {
            if(m_pLogClient->LogEntryBegin(QLog::Category::Info, L"CANVAS", szOutput))
            {
                m_pLogClient->LogEntryEnd();
            }
        }
    }

    void LogInfoF(PCWSTR szOutput, ...);
    //{
    //    va_list args;
    //    va_start(args, szOutput);
    //    LogOutputVA<SlimLog::MESSAGE>(L"CANVAS", szOutput, args);
    //    va_end(args);
    //}

    void LogDebug(PCWSTR szOutput)
    {
        if (m_pLogClient)
        {
            if (m_pLogClient->LogEntryBegin(QLog::Category::Debug, L"CANVAS", szOutput))
            {
                m_pLogClient->LogEntryEnd();
            }
        }
    }

    void LogDebugF(PCWSTR szOutput, ...);
    //{
    //    va_list args;
    //    va_start(args, szOutput);
    //    LogOutputVA<SlimLog::Info>(L"CANVAS INFO", szOutput, args);
    //    va_end(args);
    //}
};
