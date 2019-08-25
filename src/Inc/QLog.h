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

    //------------------------------------------------------------------------------------------------
    class CProperty
    {
    public:
        virtual PCWSTR GetNameString() const = 0;
        virtual PCWSTR GetValueString() const = 0;
    };

    //------------------------------------------------------------------------------------------------
    class CStringProperty : public CProperty
    {
        PCWSTR m_szName;
        PCWSTR m_szValue;
    public:
        CStringProperty(PCWSTR szName, PCWSTR szValue) :
            m_szName(szName),
            m_szValue(szValue) {}
        virtual PCWSTR GetNameString() const { return m_szName; }
        virtual PCWSTR GetValueString() const { return m_szValue; }
    };

    //------------------------------------------------------------------------------------------------
    class CLogOutput
    {
    public:
        virtual ~CLogOutput() {}
        virtual void OutputBegin(LogCategory Category, PCWSTR szLogSource, PCWSTR szMessage) = 0;
        virtual void OutputProperty(PCWSTR szName, PCWSTR szValue) = 0;
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
        virtual void Write(LogCategory Category, PCWSTR szLogSource, PCWSTR szMessage, UINT NumProperties, CProperty *pProperties[]) = 0;
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
