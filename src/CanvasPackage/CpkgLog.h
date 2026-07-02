//================================================================================================
// CpkgLog - error reporting for CanvasPackage internals: the PackageLogFn forwarding helper and
// the exception type the write path throws on failure.
//
// Composition is intentionally NOT done in LogF: the helper hands the format string and va_list
// to the sink, which applies its level filter first and only then formats (e.g.
// QLog::Logger::Log). A filtered-out record therefore costs nothing beyond the call. An empty
// PackageLogFn (the default) silences output. Internal to CanvasPackage; not part of the public
// Inc/ surface.
//================================================================================================
#pragma once

#include "CanvasPackageData.h" // Canvas::PackageLogFn / PackageLogLevel, Gem::Result

#include <cstdarg>
#include <cstdio>
#include <stdexcept>

namespace Canvas::Cpkg
{

// Forward a log record to the sink without composing it. No-op when logFn is empty.
inline void LogF(const PackageLogFn& logFn, PackageLogLevel level, const char* format, ...)
{
    if (!logFn)
        return;

    va_list args;
    va_start(args, format);
    logFn(level, format, args);
    va_end(args);
}

//--------------------------------------------------------------------------------------------------
// CpkgError - thrown by the internal write path (CCpkgSink and the chunk writers) on the first
// failure, so a long run of streaming writes needs no per-call result checks and a failure cannot
// go unnoticed. Carries the precise Gem::Result for the public API boundary to catch and return.
//--------------------------------------------------------------------------------------------------
class CpkgError : public std::runtime_error
{
public:
    CpkgError(Gem::Result result, const char* message)
        : std::runtime_error(message), m_Result(result)
    {}

    Gem::Result Result() const { return m_Result; }

private:
    Gem::Result m_Result;
};

// Log an Error-level record through the sink, then throw a CpkgError carrying result with the
// composed message as its what() string. The error path is cold, so composing here (unlike LogF)
// costs nothing that matters.
[[noreturn]] inline void ThrowF(const PackageLogFn& logFn, Gem::Result result,
                                const char* format, ...)
{
    va_list args;
    va_start(args, format);
    if (logFn)
    {
        va_list logArgs;
        va_copy(logArgs, args);
        logFn(PackageLogLevel::Error, format, logArgs);
        va_end(logArgs);
    }
    char message[512];
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    throw CpkgError(result, message);
}

} // namespace Canvas::Cpkg
