//================================================================================================
// SlimLog
//================================================================================================

#pragma once

using CanvasLogOutput = SlimLog::CLogOutputBase;

//------------------------------------------------------------------------------------------------
class CCanvasLogger : 
    public SlimLog::TLogger<CanvasLogOutput>
{
public:
    CCanvasLogger(CanvasLogOutput *pLogOutput) :
        SlimLog::TLogger<CanvasLogOutput>(pLogOutput) {}

    void LogError(PCWSTR szOutput)
    {
        LogOutput<SlimLog::LOG_CATEGORY_ERROR>(L"CANVAS ERROR", szOutput);
    }

    void LogErrorF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        LogOutputVA<SlimLog::LOG_CATEGORY_ERROR>(L"CANVAS ERROR", szOutput, args);
        va_end(args);
    }

    void LogWarning(PCWSTR szOutput)
    {
        LogOutput<SlimLog::LOG_CATEGORY_WARNING>(L"CANVAS WARNING", szOutput);
    }

    void LogWarningF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        LogOutputVA<SlimLog::LOG_CATEGORY_WARNING>(L"CANVAS WARNING", szOutput, args);
        va_end(args);
    }

    void LogMessage(PCWSTR szOutput)
    {
        LogOutput<SlimLog::LOG_CATEGORY_MESSAGE>(L"CANVAS MESSAGE", szOutput);
    }

    void LogMessageF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        LogOutputVA<SlimLog::LOG_CATEGORY_MESSAGE>(L"CANVAS MESSAGE", szOutput, args);
        va_end(args);
    }

    void LogInfo(PCWSTR szOutput)
    {
        LogOutput<SlimLog::LOG_CATEGORY_INFO>(L"CANVAS INFO", szOutput);
    }

    void LogInfoF(PCWSTR szOutput, ...)
    {
        va_list args;
        va_start(args, szOutput);
        LogOutputVA<SlimLog::LOG_CATEGORY_INFO>(L"CANVAS INFO", szOutput, args);
        va_end(args);
    }
};

//------------------------------------------------------------------------------------------------
class CDefaultLogOutput : public SlimLog::CLogOutputBase
{
    std::mutex m_Mutex;

public:
    CDefaultLogOutput() = default;

    virtual void Output(PCWSTR szHeader, PCWSTR szString)
    {
        std::unique_lock<std::mutex> lock(m_Mutex);

        // Debugger
        OutputDebugStringW(L"[");
        OutputDebugStringW(szHeader);
        OutputDebugStringW(L"] ");
        OutputDebugStringW(szString);
        OutputDebugStringW(L"[END]\n");

        // Console
        wprintf_s(L"%s: %s\n", szHeader, szString);
    }
};
