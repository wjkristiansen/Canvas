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
        std::vector<std::pair<std::wstring, std::wstring>> LogProperties;

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
                LogMessage == o.LogMessage &&
                LogProperties == o.LogProperties;
        }
    };

    class CTestLogOutput : public QLog::CLogOutput
    {
        std::deque<LogData> m_LogData;

    public:
        virtual void OutputBegin(QLog::LogCategory Category, PCWSTR szLogSource, PCWSTR szMessage)
        {
            m_LogData.emplace_back(Category, szLogSource, szMessage);
        }
        virtual void OutputProperty(PCWSTR szName, PCWSTR szValue)
        {
            m_LogData.back().LogProperties.emplace_back(std::make_pair(szName, szValue));
        }
        virtual void OutputEnd()
        {

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

        void Log(QLog::LogCategory Category, PCWSTR szLogSource, PCWSTR szMessage, UINT NumProperties = 0, QLog::CProperty *pProperties[] = nullptr)
        {
            m_pLogClient->Write(Category, szLogSource, szMessage, NumProperties, pProperties);
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

        TEST_METHOD(PropertiesLogTest)
        {
            CTestLogOutput LogOutput;
            LogData TestData[4] =
            {
                { QLog::LogCategory::Info, {L"Provider A"}, {L"Message One"}},
                { QLog::LogCategory::Error, {L"Provider A"}, {L"Message Two"}},
                { QLog::LogCategory::Verbose, {L"Provider B"}, {L"Message Three"}},
                { QLog::LogCategory::Warning, {L"Provider B"}, {L"Message Four"}},
            };

            TestData[0].LogProperties.emplace_back(std::make_pair(L"Number", L"Five"));
            TestData[0].LogProperties.emplace_back(std::make_pair(L"Letter", L"X"));

            {
                CTestLogger Logger(&LogOutput);
                QLog::CProperty *pProperties[] =
                {
                    &QLog::CStringProperty(TestData[0].LogProperties[0].first.c_str(), TestData[0].LogProperties[0].second.c_str()),
                    &QLog::CStringProperty(TestData[0].LogProperties[1].first.c_str(), TestData[0].LogProperties[1].second.c_str()),
                };
                Logger.Log(TestData[0].Category, TestData[0].LogSource.c_str(), TestData[0].LogMessage.c_str(),
                    2,
                    pProperties
                );
            }
            LogData Data;
            Assert::IsTrue(LogOutput.PopFront(Data));
            Assert::IsTrue(Data == TestData[0]);
        }
    };
}
