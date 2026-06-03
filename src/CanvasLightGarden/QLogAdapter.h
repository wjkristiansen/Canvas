// QLogAdapter.h - bridges Canvas::XLogger onto QLog::Logger.
// Mirrors the adapter used by the other Canvas sample apps so each
// executable can own its logger without forcing STL across the
// CanvasCore ABI.

#pragma once

#include "CanvasCore.h"
#include "QLog.h"
#include <memory>

namespace Canvas
{

class QLogAdapter : public Gem::TGeneric<XLogger>
{
    std::unique_ptr<QLog::Logger> m_Logger;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XLogger)
    END_GEM_INTERFACE_MAP()

    explicit QLogAdapter(QLog::Logger* logger) : m_Logger(logger) {}

    void Initialize() {}

    void Log(Canvas::LogLevel level, PCSTR format, va_list args) override
    {
        m_Logger->Log(ToQLogLevel(level), format, args);
    }

    void SetLevel(LogLevel level) override { m_Logger->SetLevel(ToQLogLevel(level)); }
    LogLevel GetLevel() override { return FromQLogLevel(m_Logger->GetLevel()); }
    void Flush() override { m_Logger->Flush(); }

private:
    static QLog::Level ToQLogLevel(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:    return QLog::Level::Trace;
        case LogLevel::Debug:    return QLog::Level::Debug;
        case LogLevel::Info:     return QLog::Level::Info;
        case LogLevel::Warn:     return QLog::Level::Warn;
        case LogLevel::Error:    return QLog::Level::Error;
        case LogLevel::Critical: return QLog::Level::Critical;
        case LogLevel::Off:      return QLog::Level::Off;
        default:                 return QLog::Level::Info;
        }
    }
    static LogLevel FromQLogLevel(QLog::Level level)
    {
        switch (level)
        {
        case QLog::Level::Trace:    return LogLevel::Trace;
        case QLog::Level::Debug:    return LogLevel::Debug;
        case QLog::Level::Info:     return LogLevel::Info;
        case QLog::Level::Warn:     return LogLevel::Warn;
        case QLog::Level::Error:    return LogLevel::Error;
        case QLog::Level::Critical: return LogLevel::Critical;
        case QLog::Level::Off:      return LogLevel::Off;
        default:                    return LogLevel::Info;
        }
    }
};

} // namespace Canvas
