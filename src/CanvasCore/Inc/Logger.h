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
            m_pLogClient->Write(QLog::LogCategory::Error, L"CANVAS", szOutput);
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
            m_pLogClient->Write(QLog::LogCategory::Warning, L"CANVAS", szOutput);
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
            m_pLogClient->Write(QLog::LogCategory::Info, L"CANVAS", szOutput);
        }
    }

    void LogInfoF(PCWSTR szOutput, ...);
    //{
    //    va_list args;
    //    va_start(args, szOutput);
    //    LogOutputVA<SlimLog::MESSAGE>(L"CANVAS", szOutput, args);
    //    va_end(args);
    //}

    void LogVerbose(PCWSTR szOutput)
    {
        if (m_pLogClient)
        {
            m_pLogClient->Write(QLog::LogCategory::Verbose, L"CANVAS", szOutput);
        }
    }

    void LogVerboseF(PCWSTR szOutput, ...);
    //{
    //    va_list args;
    //    va_start(args, szOutput);
    //    LogOutputVA<SlimLog::Info>(L"CANVAS INFO", szOutput, args);
    //    va_end(args);
    //}
};
