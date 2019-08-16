#pragma once

#define QLOGAPI __stdcall

namespace QLog
{
    enum class LogCategory
    {
        None = 0x00000000,
        Error = 0x00000001,
        Warning = 0x00000002,
        Info = 0x00000004,
        Verbose = 0x00000008,
        Mask = 0x0000000f
    };

    class CLogOutput
    {
    public:
        virtual ~CLogOutput() {}
        virtual void Write(LogCategory Category, const wchar_t *szLogSource, const wchar_t *szMessage) = 0;
    };

    class CLogHost
    {
    public:
        virtual ~CLogHost() {}
        virtual void Execute(CLogOutput *pLogOutput) = 0;
    };

    class CLogClient
    {
    public:
        virtual ~CLogClient() {}
        virtual void Write(LogCategory Category, const wchar_t *szLogSource, const wchar_t *szMessage) = 0;
    };
}

extern QLog::CLogHost * QLOGAPI QLogCreateLogHost(const wchar_t *szPipeName, unsigned int PipeBufferSize);
extern void QLOGAPI QLogDestroyLogHost(QLog::CLogHost *pLogHost);
extern QLog::CLogClient * QLOGAPI QLogCreateLogClient(const wchar_t *szPipeName);
extern void QLOGAPI QLogDestroyLogClient(QLog::CLogClient *pLogClient);
