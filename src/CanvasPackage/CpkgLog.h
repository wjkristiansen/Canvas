//================================================================================================
// CpkgLog - forwards a printf-style log record to the Canvas::PackageLogFn sink.
//
// Composition is intentionally NOT done here: the helper hands the format string and va_list to
// the sink, which applies its level filter first and only then formats (e.g. QLog::Logger::Log).
// A filtered-out record therefore costs nothing beyond the call. Internal to CanvasPackage; an
// empty PackageLogFn (the default) silences output.
//================================================================================================
#pragma once

#include "CanvasPackageData.h" // Canvas::PackageLogFn / PackageLogLevel

#include <cstdarg>

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

} // namespace Canvas::Cpkg
