#include "pch.h"

namespace QLog
{
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
            ThrowLastErrorOnFalse(WriteFile(hFile, &Len, sizeof(Len), &BytesWritten, nullptr));
            if (Len)
            {
                ThrowLastErrorOnFalse(WriteFile(hFile, szString, Len * sizeof(wchar_t), &BytesWritten, nullptr));
            }
        }

        static void Deserialize(HANDLE hFile, PWSTR szStringBuffer, DWORD BufferSizeInBytes)
        {
            DWORD BytesRead;
            DWORD OrigLen;
            ThrowLastErrorOnFalse(ReadFile(hFile, &OrigLen, sizeof(OrigLen), &BytesRead, nullptr));
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
            LogCategory Category;
            wchar_t Source[1024];
            wchar_t Message[1024];

            // Serialize messages
            try
            {
                for (;;)
                {
                    // Read the message category
                    CLogSerializer<LogCategory>::Deserialize(hPipe, Category);
                    if (LogCategory::None == Category)
                    {
                        // Terminate logger thread
                        break;
                    }

                    CLogSerializer<PWSTR >::Deserialize(hPipe, Source, 1024);
                    CLogSerializer<PWSTR >::Deserialize(hPipe, Message, 1024);

                    // Write to the log output
                    pLogOutput->Write(Category, reinterpret_cast<PCWSTR>(Source), reinterpret_cast<PWSTR >(Message));
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
            //FlushFileBuffers(m_hPipeFile);
        }

        virtual void Write(LogCategory Category, PCWSTR szLogSource, PCWSTR szMessage, UINT NumValues, CLogValue *pLogValues)
        {
            DWORD BytesWritten = 0;

            try
            {
                // Write the message category
                CLogSerializer<LogCategory>::Serialize(m_hPipeFile, Category);
                CLogSerializer<PWSTR>::Serialize(m_hPipeFile, szLogSource);
                CLogSerializer<PWSTR>::Serialize(m_hPipeFile, szMessage);
                //CLogSerializer<UINT>::Serialize(m_hPipeFile, NumValues);
                //for (UINT i = 0; i < NumValues; ++i)
                //{
                //    pLogValues[i].SerializeNameAsString(m_hPipeFile);
                //    pLogValues[i].SerializeValueAsString(m_hPipeFile);
                //}
            }
            catch (HRESULT hr)
            {
                // Write log communication error to log output...
                OutputDebugString(L"Log Write uh oh");
                throw(hr);
            }
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
