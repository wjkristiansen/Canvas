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
        virtual void OutputBegin(Category Category, PCWSTR szLogSource, PCWSTR szMessage) = 0;
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
    protected:
        UINT m_CategoryMask = 0xffffffff;

    public:
        virtual ~CLogClient() {}

        UINT SetCategoryMask(UINT Mask)
        {
            auto OldMask = m_CategoryMask;
            m_CategoryMask = Mask;
            return OldMask;
        }
        UINT GetCategoryMask() const { return m_CategoryMask; }

        virtual void Write(Category Category, PCWSTR szLogSource, PCWSTR szMessage, UINT NumProperties, const CProperty *pProperties[]) = 0;
        //virtual void WriteWithTimestamp(Category Category, PCWSTR szLogSource, PCWSTR szMessage, UINT NumProperties, const CProperty *pProperties[]) = 0;
        //virtual void WriteVA(Category Category, PCWSTR szLogSource, PCWSTR szFormatMessage, va_list args) = 0;
        //virtual void WriteWithTimestampVA(Category Category, PCWSTR szLogSource, PCWSTR szFormatMessage, va_list args) = 0;
        void Write(Category Category, PCWSTR szLogSource, PCWSTR szMessage)
        {
            Write(Category, szLogSource, szMessage, 0, nullptr);
        }
    };
}

extern QLog::CLogHost * QLOGAPI QLogCreateLogHost(PCWSTR szPipeName, unsigned int PipeBufferSize);
extern void QLOGAPI QLogDestroyLogHost(QLog::CLogHost *pLogHost);
extern QLog::CLogClient * QLOGAPI QLogCreateLogClient(PCWSTR szPipeName);
extern void QLOGAPI QLogDestroyLogClient(QLog::CLogClient *pLogClient);
