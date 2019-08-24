#pragma once

#define QLOGAPI __stdcall

namespace QLog
{
    enum class LogCategory
    {
        None = 0x00000000,
        Critical = 0x00000001,
        Error = 0x00000002,
        Warning = 0x00000004,
        Info = 0x00000008,
        Verbose = 0x00000010,
        Mask = 0x00000001f
    };

    class CLogValue
    {
    public:
        virtual void SerializeNameAsString(HANDLE hFile) = 0;
        virtual void SerializeValueAsString(HANDLE hFile) = 0;
    };

    //------------------------------------------------------------------------------------------------
    class CLogOutput
    {
    public:
        virtual ~CLogOutput() {}
        virtual void BeginOutput(LogCategory Category, PCWSTR szLogSource, PCWSTR szMessage) = 0;
        virtual void WriteValue(PCWSTR szName, PCWSTR szValue) = 0;
        virtual void EndOutput() = 0;
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
        virtual void Write(LogCategory Category, PCWSTR szLogSource, PCWSTR szMessage, UINT NumValues, CLogValue *pLogValues) = 0;
        void Write(LogCategory Category, PCWSTR szLogSource, PCWSTR szMessage)
        {
            Write(Category, szLogSource, szMessage, 0, nullptr);
        }
    };
}

extern QLog::CLogHost * QLOGAPI QLogCreateLogHost(PCWSTR szPipeName, unsigned int PipeBufferSize);
extern void QLOGAPI QLogDestroyLogHost(QLog::CLogHost *pLogHost);
extern QLog::CLogClient * QLOGAPI QLogCreateLogClient(PCWSTR szPipeName);
extern void QLOGAPI QLogDestroyLogClient(QLog::CLogClient *pLogClient);
