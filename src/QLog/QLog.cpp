#include "pch.h"

namespace QLog
{
    enum class LogTag : unsigned char
    {
        EndLog,
        BeginEntry,
        EndEntry,
        Property,
    };

    static inline void ThrowHResultError(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw(hr);
        }
    }

    static inline void ThrowLastErrorOnFalse(BOOL b)
    {
        if (!b)
        {
            auto LastError = GetLastError();
            throw(HRESULT_FROM_WIN32(LastError));
        }
    }

    template<class _Type>
    class CLogSerializer
    {
    public:
        static void Serialize(HANDLE hFile, const _Type &o)
        {
            DWORD BytesWritten;
            ThrowLastErrorOnFalse(WriteFile(hFile, &o, sizeof(o), &BytesWritten, nullptr));
        }
        static void Deserialize(HANDLE hFile, _Type &o)
        {
            DWORD BytesRead;
            ThrowLastErrorOnFalse(ReadFile(hFile, &o, sizeof(o), &BytesRead, nullptr));
        }
    };

    template<>
    class CLogSerializer<PWSTR>
    {
    public:
        static void Serialize(HANDLE hFile, PCWSTR szString)
        {
            DWORD BytesWritten;
            DWORD Len = szString ? lstrlenW(szString) : 0;
            CLogSerializer<DWORD>::Serialize(hFile, Len);
            if (Len)
            {
                ThrowLastErrorOnFalse(WriteFile(hFile, szString, Len * sizeof(wchar_t), &BytesWritten, nullptr));
            }
        }

        static void Deserialize(HANDLE hFile, PWSTR szStringBuffer, DWORD BufferSizeInBytes)
        {
            DWORD BytesRead;
            DWORD OrigLen;
            CLogSerializer<DWORD>::Deserialize(hFile, OrigLen);
            if (OrigLen)
            {
                DWORD CharsToRead = std::min<DWORD>(BufferSizeInBytes - sizeof(wchar_t), OrigLen);
                DWORD BytesToRead = CharsToRead * sizeof(wchar_t);
                ThrowLastErrorOnFalse(ReadFile(hFile, szStringBuffer, BytesToRead, &BytesRead, nullptr));
                szStringBuffer[CharsToRead] = 0; // Null terminate

                if (BytesToRead > BytesRead)
                {
                    // Move to the end of the stored string
                    SetFilePointer(hFile, BytesToRead - BytesRead, nullptr, FILE_CURRENT);
                }
            }
        }
    };

    class CLogHostImpl : public CLogHost
    {
        CScopedHandle m_hPipe;
        std::thread m_Thread;

        static void ListenerThread(CLogOutput *pLogOutput, HANDLE hPipe)
        {
            Category Cat;
            static thread_local wchar_t Buffer1[1024];
            static thread_local wchar_t Buffer2[1024];
            bool ActiveEntry = false;

            // Serialize messages
            try
            {
                for (;;)
                {
                    LogTag Tag;

                    // Get the log tag
                    CLogSerializer<LogTag>::Deserialize(hPipe, Tag);

                    if (Tag == LogTag::EndLog)
                    {
                        if (ActiveEntry)
                        {
                            throw(std::exception());
                        }

                        // Terminate the logger thread
                        break;
                    }
                    else
                    if (Tag == LogTag::BeginEntry)
                    {
                        if (ActiveEntry)
                        {
                            throw(std::exception());
                        }

                        ActiveEntry = true;

                        // Read the message category
                        CLogSerializer<Category>::Deserialize(hPipe, Cat);
                        CLogSerializer<PWSTR >::Deserialize(hPipe, Buffer1, sizeof(Buffer1));
                        CLogSerializer<PWSTR >::Deserialize(hPipe, Buffer2, sizeof(Buffer2));
                        pLogOutput->OutputBegin(Cat, reinterpret_cast<PCWSTR>(Buffer1), reinterpret_cast<PWSTR>(Buffer2));
                    }
                    else
                    if (Tag == LogTag::Property)
                    {
                        if (!ActiveEntry)
                        {
                            throw(std::exception());
                        }
                        CLogSerializer<PWSTR >::Deserialize(hPipe, Buffer1, sizeof(Buffer1));
                        CLogSerializer<PWSTR >::Deserialize(hPipe, Buffer2, sizeof(Buffer2));
                        pLogOutput->OutputProperty(Buffer1, Buffer2);
                    }
                    else
                    if(Tag == LogTag::EndEntry)
                    {
                        if (!ActiveEntry)
                        {
                            throw(std::exception());
                        }

                        // Completeexisting entry
                        pLogOutput->OutputEnd();
                        ActiveEntry = false;
                    }
                    else
                    {
                        throw(std::exception());
                    }
                }
            }
            catch (HRESULT hr)
            {
                if (hr == HRESULT_FROM_WIN32(ERROR_NO_DATA) || hr == HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE))
                {
                    // Write end of the pipe is closed
                    return;
                }

                // Write log communication error to log output...
                OutputDebugString(L"LogHost uh oh");
                throw(hr);
            }
            catch (...)
            {
                OutputDebugString(L"???");
            }
        }

    public:
        CLogHostImpl(PCWSTR szPipeName, unsigned int BufferSize)
        {
            CScopedHandle hPipe = CreateNamedPipe(
                szPipeName,
                PIPE_ACCESS_INBOUND,
                PIPE_READMODE_BYTE | PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_ACCEPT_REMOTE_CLIENTS,
                1,
                0,
                BufferSize,
                NMPWAIT_USE_DEFAULT_WAIT, nullptr);

            ThrowLastErrorOnFalse(hPipe.Get() != NULL);

            m_hPipe = hPipe.Detach();
        };

        virtual void FlushAndFinish()
        {
            FlushFileBuffers(m_hPipe);
            if (m_Thread.joinable())
            {
                // Wait for thread to finish
                m_Thread.join();
            }
        }

        ~CLogHostImpl()
        {
            FlushAndFinish();
        }

        void Execute(CLogOutput *pLogOutput)
        {
			// Wait for connection...
			bool Connect = ConnectNamedPipe(m_hPipe, nullptr);
			if (!Connect)
			{
				auto LastError = GetLastError();

				if (LastError != ERROR_PIPE_CONNECTED)
				{
					throw(HRESULT_FROM_WIN32(LastError));
				}
			}

			m_Thread = std::thread(ListenerThread, pLogOutput, m_hPipe.Get());
        }
    };

    class CLogClientImpl : public CLogClient
    {
        CScopedHandle m_hPipeFile;
        bool m_ActiveEntry = false;

    public:
        CLogClientImpl(PCWSTR szPipeName)
        {
            // Connect to the log host via pipe name
            CScopedHandle hPipeFile = CreateFile(
                szPipeName,
                GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
				FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
                NULL);

			if (INVALID_HANDLE_VALUE == hPipeFile.Get())
			{
				HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
				ThrowHResultError(hr);
			}

            m_hPipeFile = hPipeFile.Detach();
        }
        ~CLogClientImpl()
        {
            CLogSerializer<LogTag>::Serialize(m_hPipeFile, LogTag::EndLog);
        }

        void LogEntryBeginImpl(Category Cat, PCWSTR szLogSource, PCWSTR szMessage)
        {
            try
            {
                // Write the tag for new log entry
                CLogSerializer<LogTag>::Serialize(m_hPipeFile, LogTag::BeginEntry);

                // Write the message category, source and message
                CLogSerializer<Category>::Serialize(m_hPipeFile, Cat);
                CLogSerializer<PWSTR>::Serialize(m_hPipeFile, szLogSource);
                CLogSerializer<PWSTR>::Serialize(m_hPipeFile, szMessage);
            }
            catch (HRESULT hr)
            {
                // Write log communication error to log output...
                OutputDebugString(L"Log Write uh oh");
                throw(hr);
            }
        }

        virtual bool LogEntryBegin(Category Cat, PCWSTR szLogSource, PCWSTR szMessage)
        {
            if (m_ActiveEntry)
            {
                throw std::exception();
            }

            if (0 == (m_CategoryMask & UINT(Cat)))
            {
                // Discard write
                return false;
            }

            m_ActiveEntry = true;

            LogEntryBeginImpl(Cat, szLogSource, szMessage);

            return true;
        }

        virtual bool LogEntryBeginVA(Category Cat, PCWSTR szLogSource, PCWSTR szFormat, va_list args)
        {
            if (m_ActiveEntry)
            {
                throw std::exception();
            }

            m_ActiveEntry = true;

            if (0 == (m_CategoryMask & UINT(Cat)))
            {
                // Discard write
                return false;
            }

            return true;
        }

        virtual void LogEntryAddProperty(PCWSTR szName, PCWSTR szValue)
        {
            try
            {
                // Write the property tag
                CLogSerializer<LogTag>::Serialize(m_hPipeFile, LogTag::Property);

                CLogSerializer<PWSTR>::Serialize(m_hPipeFile, szName);
                CLogSerializer<PWSTR>::Serialize(m_hPipeFile, szValue);
            }
            catch (HRESULT hr)
            {
                // Write log communication error to log output...
                OutputDebugString(L"Log Write uh oh");
                throw(hr);
            }
        }

        virtual void LogEntryEnd()
        {
            if (!m_ActiveEntry)
            {
                throw std::exception();
            }

            try
            {
                CLogSerializer<LogTag>::Serialize(m_hPipeFile, LogTag::EndEntry);
            }
            catch (HRESULT hr)
            {
                // Write log communication error to log output...
                OutputDebugString(L"Log Write uh oh");
                throw(hr);
            }
    
            m_ActiveEntry = false;
        }
    };
}

QLog::CLogHost *QLogCreateLogHost(PCWSTR szPipeName, unsigned int PipeBufferSize)
{
    return new QLog::CLogHostImpl(szPipeName, PipeBufferSize);
}

void QLogDestroyLogHost(QLog::CLogHost *pLogHost)
{
    delete(pLogHost);
}
QLog::CLogClient *QLogCreateLogClient(PCWSTR szPipeName)
{
    return new QLog::CLogClientImpl(szPipeName);
}
void QLogDestroyLogClient(QLog::CLogClient *pLogClient)
{
    delete(pLogClient);
}

