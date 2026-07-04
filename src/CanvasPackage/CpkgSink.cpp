#include "pch.h"
#include "CpkgSink.h"
#include "CpkgLog.h"

#include <cstring>
#include <filesystem>
#include <ios>

namespace Canvas::Cpkg
{

CCpkgSink::~CCpkgSink()
{
    Close();
}

Gem::Result CCpkgSink::CreateFile(const char* pFilePath, CCpkgSink* pOut, size_t flushBufferSize,
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

    // Reset any prior backing so a CCpkgSink can be reused; a failure flushing an abandoned earlier
    // file does not affect the new target, so its result is discarded.
    pOut->Close();

    pOut->m_Stream.open(std::filesystem::u8path(pFilePath),
                        std::ios::binary | std::ios::out | std::ios::trunc);
    if (!pOut->m_Stream.is_open())
    {
        LogF(logFn, PackageLogLevel::Error, "CCpkgSink::CreateFile: cannot open file for writing");
        return Gem::Result::NotFound;
    }

    pOut->m_Buffer.assign(flushBufferSize ? flushBufferSize : kDefaultFlushBufferSize, uint8_t(0));
    pOut->m_BufferUsed = 0;
    pOut->m_Written    = 0;
    pOut->m_LogFn      = logFn;
    pOut->m_Backing    = Backing::File;
    return Gem::Result::Success;
}

Gem::Result CCpkgSink::FlushBuffer()
{
    if (m_BufferUsed == 0)
        return Gem::Result::Success;

    m_Stream.write(reinterpret_cast<const char*>(m_Buffer.data()),
                   static_cast<std::streamsize>(m_BufferUsed));
    if (!m_Stream)
    {
        LogF(m_LogFn, PackageLogLevel::Error,
             "CCpkgSink: failed flushing %zu bytes to disk", m_BufferUsed);
        return Gem::Result::Fail;
    }
    m_BufferUsed = 0;
    return Gem::Result::Success;
}

void CCpkgSink::WriteBytes(const void* data, size_t count)
{
    if (count == 0)
        return;

    if (m_Backing != Backing::File)
        ThrowF(m_LogFn, Gem::Result::Uninitialized, "CCpkgSink: write to an unopened sink");

    const uint8_t* src = static_cast<const uint8_t*>(data);

    // A write at least as large as the cache bypasses it: flush what is cached, then stream the
    // payload straight to disk so a big blob is not chopped into cache-sized copies.
    if (count >= m_Buffer.size())
    {
        if (Gem::Failed(FlushBuffer())) // FlushBuffer logged the detail
            throw CpkgError(Gem::Result::Fail, "CCpkgSink: cache flush failed before a large write");
        m_Stream.write(reinterpret_cast<const char*>(src), static_cast<std::streamsize>(count));
        if (!m_Stream)
            ThrowF(m_LogFn, Gem::Result::Fail, "CCpkgSink: failed writing %zu bytes to disk", count);
        m_Written += count;
        return;
    }

    // Otherwise accumulate into the cache, flushing a full block first if it would not fit.
    if (m_BufferUsed + count > m_Buffer.size())
    {
        if (Gem::Failed(FlushBuffer())) // FlushBuffer logged the detail
            throw CpkgError(Gem::Result::Fail, "CCpkgSink: cache flush failed");
    }
    std::memcpy(m_Buffer.data() + m_BufferUsed, src, count);
    m_BufferUsed += count;
    m_Written    += count;
}

void CCpkgSink::PadToAlignment(size_t alignment)
{
    if (alignment <= 1)
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
        pad -= n;
    }
}

Gem::Result CCpkgSink::PatchBytes(uint64_t offset, const void* data, size_t size)
{
    if (size == 0)
        return Gem::Result::Success;

    if (m_Backing != Backing::File)
    {
        LogF(m_LogFn, PackageLogLevel::Error, "CCpkgSink::PatchBytes: write to an unopened sink");
        return Gem::Result::Uninitialized;
    }

    // The patch target must lie within bytes already produced.
    if (offset > m_Written || size > m_Written - offset)
    {
        LogF(m_LogFn, PackageLogLevel::Error,
             "CCpkgSink::PatchBytes: range [%llu,%llu) past end of written data (%llu bytes)",
             static_cast<unsigned long long>(offset),
             static_cast<unsigned long long>(offset + size),
             static_cast<unsigned long long>(m_Written));
        return Gem::Result::InvalidArg;
    }

    // Flush so the file reflects everything up to m_Written, then seek-patch and restore the
    // append position to the end (where the next streamed write belongs).
    if (Gem::Failed(FlushBuffer()))
        return Gem::Result::Fail;
    m_Stream.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    m_Stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    m_Stream.seekp(static_cast<std::streamoff>(m_Written), std::ios::beg);
    if (!m_Stream)
    {
        LogF(m_LogFn, PackageLogLevel::Error,
             "CCpkgSink::PatchBytes: failed patching %zu bytes at offset %llu", size,
             static_cast<unsigned long long>(offset));
        return Gem::Result::Fail;
    }
    return Gem::Result::Success;
}

Gem::Result CCpkgSink::Flush()
{
    if (m_Backing != Backing::File)
        return Gem::Result::Success;

    if (Gem::Failed(FlushBuffer()))
        return Gem::Result::Fail;
    m_Stream.flush();
    if (!m_Stream)
    {
        LogF(m_LogFn, PackageLogLevel::Error, "CCpkgSink: failed flushing the stream to disk");
        return Gem::Result::Fail;
    }
    return Gem::Result::Success;
}

Gem::Result CCpkgSink::Close()
{
    if (m_Backing != Backing::File)
        return Gem::Result::Success;
    m_Backing = Backing::Empty;

    // Drain the cache and close inline rather than via FlushBuffer, so the file handle is
    // released and the stream reset for reuse even when the final writes fail; the failure is still
    // reported below.
    const size_t cached = m_BufferUsed;
    if (cached != 0)
    {
        m_Stream.write(reinterpret_cast<const char*>(m_Buffer.data()),
                       static_cast<std::streamsize>(cached));
        m_BufferUsed = 0;
    }
    m_Stream.flush();
    m_Stream.close();
    const bool failed = m_Stream.fail();
    m_Stream.clear();
    if (failed)
    {
        LogF(m_LogFn, PackageLogLevel::Error,
             "CCpkgSink: failed flushing %zu cached bytes / closing the file", cached);
        return Gem::Result::Fail;
    }
    return Gem::Result::Success;
}

} // namespace Canvas::Cpkg
