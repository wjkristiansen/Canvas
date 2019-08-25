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
            m_pLogClient->Write(QLog::Category::Error, L"CANVAS", szOutput);
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
            m_pLogClient->Write(QLog::Category::Warning, L"CANVAS", szOutput);
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
            m_pLogClient->Write(QLog::Category::Info, L"CANVAS", szOutput);
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
            m_pLogClient->Write(QLog::Category::Debug, L"CANVAS", szOutput);
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
