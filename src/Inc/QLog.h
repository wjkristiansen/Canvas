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
}

extern QLog::CLogHost * QLOGAPI QLogCreateLogHost(PCSTR szPipeName, unsigned int PipeBufferSize);
extern void QLOGAPI QLogDestroyLogHost(QLog::CLogHost *pLogHost);
extern QLog::CLogClient * QLOGAPI QLogCreateLogClient(PCSTR szPipeName);
extern void QLOGAPI QLogDestroyLogClient(QLog::CLogClient *pLogClient);
