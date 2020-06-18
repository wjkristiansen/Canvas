#pragma once

#define QLOGAPI __stdcall

namespace QLog
{
    enum class Category : UINT
    {
        None = 0x00000000,
        Critical = 0x00000001,
        Error = 0x00000002,
        Warning = 0x00000004,
        Info = 0x00000008,
        Debug = 0x00000010,
    };
    
    inline UINT operator|(Category a, Category b) { return UINT(a) | UINT(b); }
    inline UINT operator|(UINT a, Category b) { return a | UINT(b); }
    inline UINT operator|(Category a, UINT b) { return UINT(a) | b; }
    inline UINT operator&(Category a, Category b) { return UINT(a) & UINT(b); }
    inline UINT operator&(UINT a, Category b) { return a & UINT(b); }
    inline UINT operator&(Category a, UINT b) { return UINT(a) & b; }

    const UINT CategoryMaskAllErrors(Category::Error | Category::Critical);
    const UINT CategoryMaskAlerts(CategoryMaskAllErrors | Category::Warning);
    const UINT CategoryMaskNotice(CategoryMaskAlerts | Category::Info);
    const UINT CategoryMaskAll(0xffffffff);

    //------------------------------------------------------------------------------------------------
    class CLogOutput
    {
    public:
        virtual ~CLogOutput() {}
        virtual void OutputBegin(Category LogCategory, PCSTR szLogSource, PCSTR szMessage) = 0;
        virtual void OutputProperty(PCSTR szName, PCSTR szValue) = 0;
        virtual void OutputEnd() = 0;
    };

    //------------------------------------------------------------------------------------------------
    class CLogHost
    {
    public:
        virtual ~CLogHost() {}
        virtual void Execute(CLogOutput *pLogOutput) = 0;
        virtual void FlushAndFinish() = 0;
    };

    class CLogClient
    {
    public:
        virtual ~CLogClient() {}

        virtual UINT SetCategoryMask(UINT Mask) = 0;
        virtual UINT GetCategoryMask() const = 0;

        virtual bool LogEntryBegin(Category LogCategory, PCSTR szLogSource, PCSTR szMessage) = 0;
        virtual bool LogEntryBeginVA(Category LogCategory, PCSTR szLogSource, PCSTR szFormat, va_list args) = 0;
        virtual void LogEntryAddProperty(PCSTR szName, PCSTR szValue) = 0;
        virtual void LogEntryEnd() = 0;
    };

    //------------------------------------------------------------------------------------------------
    class CBasicLogger
    {
        QLog::CLogClient *m_pLogClient;
        PCSTR m_szSourceName;

    public:
        CBasicLogger(QLog::CLogClient *pLogClient, PCSTR szSourceName) :
            m_pLogClient(pLogClient) {}

        QLog::CLogClient *GetLogClient() const { return m_pLogClient; }

        void Log(QLog::Category LogCategory, PCSTR szOutput)
        {
            if (m_pLogClient)
            {
                if (m_pLogClient->LogEntryBegin(LogCategory, m_szSourceName, szOutput))
                {
                    m_pLogClient->LogEntryEnd();
                }
            }
        }

        void Log(QLog::Category LogCategory, PCSTR szFormat, va_list args)
        {
            if (m_pLogClient)
            {
                if (m_pLogClient->LogEntryBeginVA(LogCategory, m_szSourceName, szFormat, args))
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
}

extern QLog::CLogHost * QLOGAPI QLogCreateLogHost(PCSTR szPipeName, unsigned int PipeBufferSize);
extern void QLOGAPI QLogDestroyLogHost(QLog::CLogHost *pLogHost);
extern QLog::CLogClient * QLOGAPI QLogCreateLogClient(PCSTR szPipeName);
extern void QLOGAPI QLogDestroyLogClient(QLog::CLogClient *pLogClient);
