//================================================================================================
// Diagnostics - reports cpkg's findings to standard error
//
// See "Diagnostics" in src/cpkg/README.md for the line format.
//================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace CpkgTool
{

enum class Severity
{
    Error,
    Warning,
    Info,
};

// Writes diagnostics, one per line. With werror a warning is reported as an error, and with
// quiet only errors are written.
class CDiagnostics
{
public:
    CDiagnostics(std::ostream &stream, bool werror, bool quiet);

    // Reports a finding about a file as a whole, such as a .cpkg file. file is a UTF-8 path.
    void Report(Severity severity, std::string_view file, std::string_view message);

    // Reports a finding at a value of a JSON file. pointer is the JSON Pointer to the value.
    void Report(Severity severity, std::string_view file, uint32_t line, uint32_t column, std::string_view message, std::string_view pointer);

    // Reports a finding at a place in a JSON file that is not a value, such as a syntax error.
    void Report(Severity severity, std::string_view file, uint32_t line, uint32_t column, std::string_view message);

    // Records each warning reported from now on in *pLog, before --werror applies, in the line
    // form with its file path absolute. Null stops recording.
    void LogWarnings(std::vector<std::string> *pLog) { m_pWarningLog = pLog; }

    // Reports again a warning LogWarnings recorded: logged is the entry, and file its file path,
    // with which the entry begins. Returns false, reporting nothing, for an entry of another form.
    bool ReportLogged(std::string_view file, std::string_view logged);

    // Writes text, the diagnostics another CDiagnostics wrote, counting its errorCount errors.
    void Append(std::string_view text, size_t errorCount);

    size_t GetErrorCount() const { return m_ErrorCount; }
    bool IsWerror() const { return m_Werror; }
    bool IsQuiet() const { return m_Quiet; }

private:
    // Reports the diagnostic whose line is <file><location>: <severity>: <text>.
    void Emit(Severity severity, std::string_view file, std::string_view location, std::string_view text);

    std::ostream &m_Stream;
    bool m_Werror;
    bool m_Quiet;
    size_t m_ErrorCount = 0;
    std::vector<std::string> *m_pWarningLog = nullptr;
};

// The UTF-8 path relative to the current directory, with '/' separators. A path with no
// relative form, such as one on another drive, is returned absolute.
std::string DisplayPath(std::string_view path);

// The UTF-8 path made absolute against the current directory, with '/' separators and its "."
// and ".." segments removed.
std::string AbsolutePath(std::string_view path);

// The text as a JSON string literal: in quotes, with '"', '\', and control characters escaped.
// Other bytes are written unchanged.
std::string Quoted(std::string_view text);

} // namespace CpkgTool
