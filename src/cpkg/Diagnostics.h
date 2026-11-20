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

    size_t GetErrorCount() const { return m_ErrorCount; }

private:
    Severity Apply(Severity severity);
    bool IsWritten(Severity severity) const;

    std::ostream &m_Stream;
    bool m_Werror;
    bool m_Quiet;
    size_t m_ErrorCount = 0;
};

// The UTF-8 path relative to the current directory, with '/' separators. A path with no
// relative form, such as one on another drive, is returned absolute.
std::string DisplayPath(std::string_view path);

// The text as a JSON string literal: in quotes, with '"', '\', and control characters escaped.
// Other bytes are written unchanged.
std::string Quoted(std::string_view text);

} // namespace CpkgTool
