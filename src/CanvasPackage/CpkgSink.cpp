#include "pch.h"
#include "CpkgSink.h"
#include "CpkgLog.h"

#include <cstring>
#include <ios>

namespace Canvas::Cpkg
{

CCpkgSink::~CCpkgSink()
{
    Close();
}

Gem::Result CCpkgSink::CreateFile(const wchar_t* pFilePath, CCpkgSink* pOut, size_t flushBufferSize,
                                 const PackageLogFn& logFn)
{
    if (!pOut)
    {
        LogF(logFn, PackageLogLevel::Error, "CCpkgSink::CreateFile: null output pointer");
        return Gem::Result::BadPointer;
    }
    if (!pFilePath)
    {
        LogF(logFn, PackageLogLevel::Error, "CCpkgSink::CreateFile: null file path");
        return Gem::Result::BadPointer;
    }

    pOut->Close(); // reset any prior backing so a CCpkgSink can be reused

    pOut->m_Stream.open(pFilePath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!pOut->m_Stream.is_open())
    {
        LogF(logFn, PackageLogLevel::Error, "CCpkgSink::CreateFile: cannot open file for writing");
        return Gem::Result::NotFound;
    }

    pOut->m_Buffer.assign(flushBufferSize ? flushBufferSize : kDefaultFlushBufferSize, uint8_t(0));
    pOut->m_BufferUsed = 0;
    pOut->m_Written    = 0;
    pOut->m_Status     = Gem::Result::Success;
    pOut->m_LogFn      = logFn;
    pOut->m_Backing    = Backing::File;
    return Gem::Result::Success;
}

void CCpkgSink::Latch(Gem::Result r)
{
    if (Gem::Succeeded(m_Status)) // record only the first failure
        m_Status = r;
}

void CCpkgSink::FlushBuffer()
{
    if (Gem::Failed(m_Status) || m_BufferUsed == 0)
        return;

    m_Stream.write(reinterpret_cast<const char*>(m_Buffer.data()),
                   static_cast<std::streamsize>(m_BufferUsed));
    if (!m_Stream)
    {
        LogF(m_LogFn, PackageLogLevel::Error,
             "CCpkgSink: failed flushing %zu bytes to disk", m_BufferUsed);
        Latch(Gem::Result::Fail);
        return;
    }
    m_BufferUsed = 0;
}

void CCpkgSink::WriteBytes(const void* data, size_t count)
{
    if (Gem::Failed(m_Status) || count == 0)
        return;

    if (m_Backing != Backing::File)
    {
        LogF(m_LogFn, PackageLogLevel::Error, "CCpkgSink: write to an unopened sink");
        Latch(Gem::Result::Uninitialized);
        return;
    }

    const uint8_t* src = static_cast<const uint8_t*>(data);

    // A write at least as large as the cache bypasses it: flush what is cached, then stream the
    // payload straight to disk so a big blob is not chopped into cache-sized copies.
    if (count >= m_Buffer.size())
    {
        FlushBuffer();
        if (Gem::Failed(m_Status))
            return;

        m_Stream.write(reinterpret_cast<const char*>(src), static_cast<std::streamsize>(count));
        if (!m_Stream)
        {
            LogF(m_LogFn, PackageLogLevel::Error, "CCpkgSink: failed writing %zu bytes to disk", count);
            Latch(Gem::Result::Fail);
            return;
        }
        m_Written += count;
        return;
    }

    // Otherwise accumulate into the cache, flushing a full block first if it would not fit.
    if (m_BufferUsed + count > m_Buffer.size())
    {
        FlushBuffer();
        if (Gem::Failed(m_Status))
            return;
    }
    std::memcpy(m_Buffer.data() + m_BufferUsed, src, count);
    m_BufferUsed += count;
    m_Written    += count;
}

void CCpkgSink::PadToAlignment(size_t alignment)
{
    if (Gem::Failed(m_Status) || alignment <= 1)
        return;

    size_t rem = static_cast<size_t>(m_Written % alignment);
    if (rem == 0)
        return;

    size_t pad = alignment - rem;
    static const uint8_t zeros[64] = {};
    while (pad > 0)
    {
        size_t n = (pad < sizeof zeros) ? pad : sizeof zeros;
        WriteBytes(zeros, n);
        if (Gem::Failed(m_Status))
            return;
        pad -= n;
    }
}

Gem::Result CCpkgSink::PatchBytes(uint64_t offset, const void* data, size_t size)
{
    if (Gem::Failed(m_Status))
        return m_Status;
    if (size == 0)
        return Gem::Result::Success;

    if (m_Backing != Backing::File)
    {
        LogF(m_LogFn, PackageLogLevel::Error, "CCpkgSink::PatchBytes: write to an unopened sink");
        Latch(Gem::Result::Uninitialized);
        return m_Status;
    }

    // The patch target must lie within bytes already produced.
    if (offset > m_Written || size > m_Written - offset)
    {
        LogF(m_LogFn, PackageLogLevel::Error,
             "CCpkgSink::PatchBytes: range [%llu,%llu) past end of written data (%llu bytes)",
             static_cast<unsigned long long>(offset),
             static_cast<unsigned long long>(offset + size),
             static_cast<unsigned long long>(m_Written));
        Latch(Gem::Result::InvalidArg);
        return m_Status;
    }

    // Flush so the file reflects everything up to m_Written, then seek-patch and restore the
    // append position to the end (where the next streamed write belongs).
    FlushBuffer();
    if (Gem::Failed(m_Status))
        return m_Status;

    m_Stream.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    m_Stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    m_Stream.seekp(static_cast<std::streamoff>(m_Written), std::ios::beg);
    if (!m_Stream)
    {
        LogF(m_LogFn, PackageLogLevel::Error,
             "CCpkgSink::PatchBytes: failed patching %zu bytes at offset %llu", size,
             static_cast<unsigned long long>(offset));
        Latch(Gem::Result::Fail);
    }
    return m_Status;
}

Gem::Result CCpkgSink::Flush()
{
    FlushBuffer();
    if (m_Backing == Backing::File)
        m_Stream.flush();
    return m_Status;
}

Gem::Result CCpkgSink::Close()
{
    if (m_Backing == Backing::File)
    {
        FlushBuffer();
        m_Stream.flush();
        m_Stream.close();
        m_Backing = Backing::Empty;
    }
    return m_Status;
}

} // namespace Canvas::Cpkg
