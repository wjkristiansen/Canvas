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
            throw(HRESULT_FROM_WIN32(GetLastError()));
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
    class CLogSerializer<wchar_t *>
    {
    public:
        static void Serialize(HANDLE hFile, const wchar_t *szString, DWORD MaxChars)
        {
            DWORD BytesWritten;
            DWORD Len = std::min<DWORD>(MaxChars, szString ? sizeof(wchar_t) * lstrlenW(szString) : 0);
            ThrowLastErrorOnFalse(WriteFile(hFile, &Len, sizeof(Len), &BytesWritten, nullptr));
            if (Len)
            {
                ThrowLastErrorOnFalse(WriteFile(hFile, szString, Len * sizeof(wchar_t), &BytesWritten, nullptr));
            }
        }

        static void Deserialize(HANDLE hFile, wchar_t *szString, DWORD MaxChars)
        {
            DWORD BytesRead;
            DWORD Len;
            ThrowLastErrorOnFalse(ReadFile(hFile, &Len, sizeof(Len), &BytesRead, nullptr));
            Len = std::min<DWORD>(MaxChars - 1, Len);
            if (Len)
            {
                ThrowLastErrorOnFalse(ReadFile(hFile, szString, Len * sizeof(wchar_t), &BytesRead, nullptr));
                szString[Len] = 0; // Null terminate
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
                bool Connect = ConnectNamedPipe(hPipe, nullptr);
                if (!Connect)
                {
                    auto LastError = GetLastError();

                    if (LastError != ERROR_PIPE_CONNECTED)
                    {
                        throw(HRESULT_FROM_WIN32(LastError));
                    }
                }

                for (;;)
                {
                    Sleep(10);

                    // Read the message category
                    CLogSerializer<LogCategory>::Deserialize(hPipe, Category);
                    if (LogCategory::None == Category)
                    {
                        // Terminate logger thread
                        break;
                    }

                    CLogSerializer<wchar_t *>::Deserialize(hPipe, Source, 1024);
                    CLogSerializer<wchar_t *>::Deserialize(hPipe, Message, 1024);

                    // Write to the log output
                    pLogOutput->Write(Category, reinterpret_cast<const wchar_t *>(Source), reinterpret_cast<wchar_t *>(Message));
                }
            }
            catch (HRESULT hr)
            {
                // Write log communication error to log output...
                OutputDebugString(L"LogHost uh oh");
                throw(hr);
            }
            catch (...)
            {
                OutputDebugString(L"???");
            }

            FlushFileBuffers(hPipe);
        }

    public:
        CLogHostImpl(const wchar_t *szPipeName, unsigned int BufferSize)
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
        ~CLogHostImpl()
        {
            if (m_Thread.joinable())
            {
                // Wait for thread to finish
                m_Thread.join();
            }
        }

        void Execute(CLogOutput *pLogOutput)
        {
            m_Thread = std::thread(ListenerThread, pLogOutput, m_hPipe.Get());
        }


    };

    class CLogClientImpl : public CLogClient
    {
        HANDLE m_hPipeFile;

    public:
        CLogClientImpl(const wchar_t *szPipeName)
        {
            // Connect to the log host via pipe name
            CScopedHandle hPipeFile = CreateFile(
                szPipeName,
                GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                NULL);

            ThrowLastErrorOnFalse(hPipeFile.Get() != NULL);

            m_hPipeFile = hPipeFile.Detach();
        }
        ~CLogClientImpl()
        {
        }

        virtual void Write(LogCategory Category, const wchar_t *szLogSource, const wchar_t *szMessage)
        {
            DWORD BytesWritten = 0;

            try
            {
                // Write the message category
                CLogSerializer<LogCategory>::Serialize(m_hPipeFile, Category);
                CLogSerializer<wchar_t *>::Serialize(m_hPipeFile, szLogSource, 1023);
                CLogSerializer<wchar_t *>::Serialize(m_hPipeFile, szMessage, 1023);
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

QLog::CLogHost *QLogCreateLogHost(const wchar_t *szPipeName, unsigned int PipeBufferSize)
{
    return new QLog::CLogHostImpl(szPipeName, PipeBufferSize);
}

void QLogDestroyLogHost(QLog::CLogHost *pLogHost)
{
    delete(pLogHost);
}
QLog::CLogClient *QLogCreateLogClient(const wchar_t *szPipeName)
{
    return new QLog::CLogClientImpl(szPipeName);
}
void QLogDestroyLogClient(QLog::CLogClient *pLogClient)
{
    delete(pLogClient);
}
