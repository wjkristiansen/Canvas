#include "pch.h"
#include "CpkgSource.h"
#include "CpkgLog.h"

#include <ios>

namespace Canvas::Cpkg
{

Gem::Result CCpkgSource::OpenFile(const wchar_t* pFilePath, CCpkgSource* pOut, const PackageLogFn& logFn)
{
    if (!pOut)
    {
        LogF(logFn, PackageLogLevel::Error, "CCpkgSource::OpenFile: null output pointer");
        return Gem::Result::BadPointer;
    }
    if (!pFilePath)
    {
        LogF(logFn, PackageLogLevel::Error, "CCpkgSource::OpenFile: null file path");
        return Gem::Result::BadPointer;
    }

    CCpkgSource source;

    // ate: seek to end on open so tellg gives the file size in one step.
    source.m_Stream.open(pFilePath, std::ios::binary | std::ios::ate);
    if (!source.m_Stream.is_open())
    {
        LogF(logFn, PackageLogLevel::Error, "CCpkgSource::OpenFile: cannot open file for reading");
        return Gem::Result::NotFound;
    }

    const std::streamoff end = source.m_Stream.tellg();
    if (end < 0)
    {
        LogF(logFn, PackageLogLevel::Error, "CCpkgSource::OpenFile: cannot determine file size");
        return Gem::Result::Fail;
    }
    source.m_Size    = static_cast<uint64_t>(end);
    source.m_Backing = Backing::File;

    *pOut = std::move(source);
    return Gem::Result::Success;
}

Gem::Result CCpkgSource::Read(uint64_t offset, void* dst, size_t size)
{
    if (size == 0)
        return (offset <= m_Size) ? Gem::Result::Success : Gem::Result::InvalidArg;

    // Requested range lies outside the source: a bad request from the caller.
    if (offset > m_Size || size > m_Size - offset)
        return Gem::Result::InvalidArg;

    switch (m_Backing)
    {
    case Backing::File:
        m_Stream.clear(); // clear any prior eof/fail state before seeking
        m_Stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        m_Stream.read(static_cast<char*>(dst), static_cast<std::streamsize>(size));
        // Range was already validated against Size(), so a short read is an actual I/O failure.
        return (m_Stream.gcount() == static_cast<std::streamsize>(size))
                   ? Gem::Result::Success
                   : Gem::Result::Fail;

    default:
        return Gem::Result::Uninitialized; // empty (default-constructed) source
    }
}

} // namespace Canvas::Cpkg
