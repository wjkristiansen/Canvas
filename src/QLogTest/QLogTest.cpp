#include "pch.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace QLogTest
{
    class QLogHostDeleter
    {
    public:
        void operator()(QLog::CLogHost *p)
        {
            QLogDestroyLogHost(p);
        }
    };

    class QLogClientDeleter
    {
    public:
        void operator()(QLog::CLogClient *p)
        {
            QLogDestroyLogClient(p);
        }
    };

    struct LogData
    {
        QLog::LogCategory Category = QLog::LogCategory::None;
        std::wstring LogSource;
        std::wstring LogMessage;

        LogData() = default;
        LogData(QLog::LogCategory category, PCWSTR szLogSource, PCWSTR szMessage) :
            Category(category),
            LogSource(szLogSource),
            LogMessage(szMessage) {}
        bool operator==(const LogData &o)
        {
            return
                Category == o.Category &&
                LogSource == o.LogSource &&
                LogMessage == o.LogMessage;
        }
    };

    class CTestLogOutput : public QLog::CLogOutput
    {
        std::deque<LogData> m_LogData;

    public:
        virtual void Write(QLog::LogCategory Category, PCWSTR szLogSource, PCWSTR szMessage)
        {
            m_LogData.emplace_back(Category, szLogSource, szMessage);
        }

        bool PopFront(LogData &Data)
        {
            if (m_LogData.empty())
            {
                return false;
            }

            Data = m_LogData.front();
            m_LogData.pop_front();
            return true;
        }
    };

    class CTestLogger
    {
        std::unique_ptr<QLog::CLogHost, QLogHostDeleter> m_pLogHost;
        std::unique_ptr<QLog::CLogClient, QLogClientDeleter> m_pLogClient;

    public:
        CTestLogger(QLog::CLogOutput *pLogOutput) :
            m_pLogHost(QLogCreateLogHost(L"\\\\.\\pipe\\Test", 4096)),
            m_pLogClient(QLogCreateLogClient(L"\\\\.\\pipe\\Test"))
        {
            m_pLogHost->Execute(pLogOutput);
        }

        ~CTestLogger()
        {
            WaitFinish();
        }

        void Log(QLog::LogCategory Category, PCWSTR szLogSource, PCWSTR szMessage)
        {
            m_pLogClient->Write(Category, szLogSource, szMessage);
        }

        void WaitFinish()
        {
//            Log(QLog::LogCategory::None, nullptr, nullptr); // Terminate logging
            m_pLogClient = nullptr;
            m_pLogHost->FlushAndFinish();
        }
    };

	TEST_CLASS(QLogTest)
	{
	public:
		TEST_METHOD(BasicLogTest)
		{
            CTestLogOutput LogOutput;
            const LogData TestData[4] =
            {
                { QLog::LogCategory::Info, {L"Provider A"}, {L"Message One"}},
                { QLog::LogCategory::Error, {L"Provider A"}, {L"Message Two"}},
                { QLog::LogCategory::Verbose, {L"Provider B"}, {L"Message Three"}},
                { QLog::LogCategory::Warning, {L"Provider B"}, {L"Message Four"}},
            };

            {
                CTestLogger Logger(&LogOutput);
                for (int i = 0; i < 4; ++i)
                {
                    Logger.Log(TestData[i].Category, TestData[i].LogSource.c_str(), TestData[i].LogMessage.c_str());
                }
            }


            LogData Data;
            for (int i = 0; i < 4; ++i)
            {
                Assert::IsTrue(LogOutput.PopFront(Data));
                Assert::IsTrue(Data == TestData[i]);
            }
            Assert::IsFalse(LogOutput.PopFront(Data));
        }
	};
}
