#pragma once

#define QLOGAPI __stdcall

namespace QLog
{
    enum LOG_CATEGORY
    {
        LOG_CATEGORY_NONE = 0x00000000,
        LOG_CATEGORY_ERROR = 0x00000001,
        LOG_CATEGORY_WARNING = 0x00000002,
        LOG_CATEGORY_INFO = 0x00000004,
        LOG_CATEGORY_VERBOSE = 0x00000008,
        LOG_CATEGORY_MASK = 0x0000000f
    };

    class CLogOutput
    {
    public:
        virtual ~CLogOutput() {}
        virtual void Write(LOG_CATEGORY Category, const wchar_t *szLogSource, const wchar_t *szMessage) = 0;
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
        virtual void Write(LOG_CATEGORY Category, const wchar_t *szLogSource, const wchar_t *szMessage) = 0;
    };
}

extern QLog::CLogHost * QLOGAPI QLogCreateLogHost(const wchar_t *szPipeName, unsigned int PipeBufferSize);
extern void QLOGAPI QLogDestroyLogHost(QLog::CLogHost *pLogHost);
extern QLog::CLogClient * QLOGAPI QLogCreateLogClient(const wchar_t *szPipeName);
extern void QLOGAPI QLogDestroyLogClient(QLog::CLogClient *pLogClient);
