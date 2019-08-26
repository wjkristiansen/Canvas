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
        QLog::Category LogCategory = QLog::Category::None;
        std::string LogSource;
        std::string LogMessage;
        std::vector<std::pair<std::string, std::string>> LogProperties;

        LogData() = default;
        LogData(QLog::Category category, PCSTR szLogSource, PCSTR szMessage) :
            LogCategory(category),
            LogSource(szLogSource),
            LogMessage(szMessage) {}
        bool operator==(const LogData &o)
        {
            return
                LogCategory == o.LogCategory &&
                LogSource == o.LogSource &&
                LogMessage == o.LogMessage &&
                LogProperties == o.LogProperties;
        }
    };

    class CTestLogOutput : public QLog::CLogOutput
    {
        std::deque<LogData> m_LogData;

    public:
        virtual void OutputBegin(QLog::Category LogCategory, PCSTR szLogSource, PCSTR szMessage)
        {
            m_LogData.emplace_back(LogCategory, szLogSource, szMessage);
        }
        virtual void OutputProperty(PCSTR szName, PCSTR szValue)
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
            m_pLogHost(QLogCreateLogHost("\\\\.\\pipe\\Test", 4096)),
            m_pLogClient(QLogCreateLogClient("\\\\.\\pipe\\Test"))
        {
            m_pLogHost->Execute(pLogOutput);
        }

        ~CTestLogger()
        {
            WaitFinish();
        }

        QLog::CLogClient *GetClient() { return m_pLogClient.get(); }

        void Write(QLog::Category LogCategory, PCSTR szSource, PCSTR szMessage)
        {
            if (m_pLogClient->LogEntryBegin(LogCategory, szSource, szMessage))
            {
                m_pLogClient->LogEntryEnd();
            }
        }

        void WaitFinish()
        {
            m_pLogClient = nullptr;
            m_pLogHost->FlushAndFinish();
        }
    };

	TEST_CLASS(QLogTest)
	{
	public:
        TEST_METHOD(BasicLogTest)
        {
            UINT LogMasks[] =
            {
                QLog::CategoryMaskAll,
                QLog::CategoryMaskAllErrors,
                UINT(QLog::Category::Warning),
                QLog::Category::Debug | QLog::Category::Critical,
                0
            };

            constexpr UINT MaskCount = sizeof(LogMasks) / sizeof(LogMasks[0]);

            for (UINT m = 0; m < MaskCount; ++m)
            {
                CTestLogOutput LogOutput;

                const LogData TestData[] =
                {
                    { QLog::Category::Info, {"Provider A"}, {"Message One"}},
                    { QLog::Category::Error, {"Provider A"}, {"Message Two"}},
                    { QLog::Category::Debug, {"Provider B"}, {"Message Three"}},
                    { QLog::Category::Warning, {"Provider B"}, {"Message Four"}},
                    { QLog::Category::Critical, {"Provider C"}, {"Message Five"}},
                };

                constexpr UINT TestDataCount = sizeof(TestData) / sizeof(TestData[0]);

                {
                    CTestLogger Logger(&LogOutput);
                    Logger.GetClient()->SetCategoryMask(LogMasks[m]);
                    for (int i = 0; i < TestDataCount; ++i)
                    {
                        Logger.Write(TestData[i].LogCategory, TestData[i].LogSource.c_str(), TestData[i].LogMessage.c_str());
                    }
                }

                LogData Data;
                for (int i = 0; i < TestDataCount; ++i)
                {
                    if (0 != (TestData[i].LogCategory & LogMasks[m]))
                    {
                        Assert::IsTrue(LogOutput.PopFront(Data));
                        Assert::IsTrue(Data == TestData[i]);
                    }
                }
                Assert::IsFalse(LogOutput.PopFront(Data));
            }
        }

        TEST_METHOD(PropertiesLogTest)
        {
            CTestLogOutput LogOutput;
            LogData TestData[4] =
            {
                { QLog::Category::Info, {"Provider A"}, {"Message One"}},
                { QLog::Category::Error, {"Provider A"}, {"Message Two"}},
                { QLog::Category::Debug, {"Provider B"}, {"Message Three"}},
                { QLog::Category::Warning, {"Provider B"}, {"Message Four"}},
            };

            TestData[0].LogProperties.emplace_back(std::make_pair("Number", "Five"));
            TestData[0].LogProperties.emplace_back(std::make_pair("Letter", "X"));

            {
                CTestLogger Logger(&LogOutput);
                Logger.GetClient()->LogEntryBegin(TestData[0].LogCategory, TestData[0].LogSource.c_str(), TestData[0].LogMessage.c_str());
                Logger.GetClient()->LogEntryAddProperty(TestData[0].LogProperties[0].first.c_str(), TestData[0].LogProperties[0].second.c_str());
                Logger.GetClient()->LogEntryAddProperty(TestData[0].LogProperties[1].first.c_str(), TestData[0].LogProperties[1].second.c_str());
                Logger.GetClient()->LogEntryEnd();
            }
            LogData Data;
            Assert::IsTrue(LogOutput.PopFront(Data));
            Assert::IsTrue(Data == TestData[0]);
        }
    };
}
