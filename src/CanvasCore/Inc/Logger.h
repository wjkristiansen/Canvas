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
            m_pLogClient->Write(QLog::LOG_CATEGORY_ERROR, L"CANVAS", szOutput);
        }
    }

    void LogErrorF(PCWSTR szOutput, ...);
    //{
    //    va_list args;
    //    va_start(args, szOutput);
    //    LogOutputVA<SlimLog::LOG_CATEGORY_ERROR>(L"CANVAS ERROR", szOutput, args);
    //    va_end(args);
    //}

    void LogWarning(PCWSTR szOutput)
    {
        if (m_pLogClient)
        {
            m_pLogClient->Write(QLog::LOG_CATEGORY_WARNING, L"CANVAS", szOutput);
        }
    }

    void LogWarningF(PCWSTR szOutput, ...);
    //{
    //    va_list args;
    //    va_start(args, szOutput);
    //    LogOutputVA<SlimLog::LOG_CATEGORY_WARNING>(L"CANVAS WARNING", szOutput, args);
    //    va_end(args);
    //}

    void LogInfo(PCWSTR szOutput)
    {
        if (m_pLogClient)
        {
            m_pLogClient->Write(QLog::LOG_CATEGORY_INFO, L"CANVAS", szOutput);
        }
    }

    void LogInfoF(PCWSTR szOutput, ...);
    //{
    //    va_list args;
    //    va_start(args, szOutput);
    //    LogOutputVA<SlimLog::LOG_CATEGORY_MESSAGE>(L"CANVAS", szOutput, args);
    //    va_end(args);
    //}

    void LogVerbose(PCWSTR szOutput)
    {
        if (m_pLogClient)
        {
            m_pLogClient->Write(QLog::LOG_CATEGORY_VERBOSE, L"CANVAS", szOutput);
        }
    }

    void LogVerboseF(PCWSTR szOutput, ...);
    //{
    //    va_list args;
    //    va_start(args, szOutput);
    //    LogOutputVA<SlimLog::LOG_CATEGORY_INFO>(L"CANVAS INFO", szOutput, args);
    //    va_end(args);
    //}
};
