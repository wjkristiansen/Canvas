// QLogAdapter.h
// Adapter that implements Canvas::XLogger using QLog as the backend
// 
// This adapter lives in the application (.exe), not in Canvas DLLs.
// It safely bridges the ABI-stable XLogger interface with QLog's STL-heavy implementation.
//
// Uses Gem::TGenericImpl for automatic reference counting and QueryInterface support.

#pragma once

#include "CanvasCore.h"
#include "QLog.h"
#include <memory>

namespace Canvas
{

// Adapter that implements XLogger using QLog
class QLogAdapter : public Gem::TGeneric<XLogger>
{
    std::unique_ptr<QLog::Logger> m_Logger;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XLogger)
    END_GEM_INTERFACE_MAP()

    // Takes ownership of the QLog logger (raw pointer)
    explicit QLogAdapter(QLog::Logger* logger)
        : m_Logger(logger)
    {
    }

    // Two-phase initialization (required by TGenericImpl)
    void Initialize()
    {
        // Nothing to initialize after construction
    }

    // XLogger interface implementation - only the core Log method
    void Log(Canvas::LogLevel level, PCSTR format, va_list args) override
    {
        m_Logger->Log(ToQLogLevel(level), format, args);
    }

    // Level control
    void SetLevel(LogLevel level) override
    {
        m_Logger->SetLevel(ToQLogLevel(level));
    }

    LogLevel GetLevel() override
    {
        return FromQLogLevel(m_Logger->GetLevel());
    }

    // Flush
    void Flush() override
    {
        m_Logger->Flush();
    }

    // Access underlying QLog logger (for advanced configuration)
    QLog::Logger* GetQLogger() { return m_Logger.get(); }
    const QLog::Logger* GetQLogger() const { return m_Logger.get(); }

private:

    // Convert between Canvas::LogLevel and QLog::Level
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
