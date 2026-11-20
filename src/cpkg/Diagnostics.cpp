//================================================================================================
// Diagnostics
//================================================================================================

#include "Diagnostics.h"

#include <filesystem>
#include <system_error>

namespace CpkgTool
{

namespace
{
    const char *SeverityName(Severity severity)
    {
        switch (severity)
        {
        case Severity::Error:
            return "error";
        case Severity::Warning:
            return "warning";
        default:
            return "info";
        }
    }
}

//------------------------------------------------------------------------------------------------
CDiagnostics::CDiagnostics(std::ostream &stream, bool werror, bool quiet) :
    m_Stream(stream),
    m_Werror(werror),
    m_Quiet(quiet)
{
}

//------------------------------------------------------------------------------------------------
Severity CDiagnostics::Apply(Severity severity)
{
    if (severity == Severity::Warning && m_Werror)
        severity = Severity::Error;
    if (severity == Severity::Error)
        ++m_ErrorCount;
    return severity;
}

//------------------------------------------------------------------------------------------------
bool CDiagnostics::IsWritten(Severity severity) const
{
    return severity == Severity::Error || !m_Quiet;
}

//------------------------------------------------------------------------------------------------
void CDiagnostics::Report(Severity severity, std::string_view file, std::string_view message)
{
    severity = Apply(severity);
    if (IsWritten(severity))
        m_Stream << DisplayPath(file) << ": " << SeverityName(severity) << ": " << message << '\n';
}

//------------------------------------------------------------------------------------------------
void CDiagnostics::Report(Severity severity, std::string_view file, uint32_t line, uint32_t column, std::string_view message, std::string_view pointer)
{
    severity = Apply(severity);
    if (IsWritten(severity))
    {
        m_Stream << DisplayPath(file) << ':' << line << ':' << column << ": " << SeverityName(severity) << ": " << message
                 << " [" << pointer << "]\n";
    }
}

//------------------------------------------------------------------------------------------------
void CDiagnostics::Report(Severity severity, std::string_view file, uint32_t line, uint32_t column, std::string_view message)
{
    severity = Apply(severity);
    if (IsWritten(severity))
        m_Stream << DisplayPath(file) << ':' << line << ':' << column << ": " << SeverityName(severity) << ": " << message << '\n';
}

//------------------------------------------------------------------------------------------------
std::string DisplayPath(std::string_view path)
{
    namespace fs = std::filesystem;
    const fs::path given(std::u8string_view(reinterpret_cast<const char8_t *>(path.data()), path.size()));

    std::error_code ec;
    const fs::path current = fs::current_path(ec);
    fs::path shown = given;
    if (!ec)
    {
        const fs::path absolute = fs::absolute(given, ec);
        if (!ec)
            shown = absolute.lexically_normal().lexically_proximate(current);
    }

    const std::u8string utf8 = shown.generic_u8string();
    return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
}

//------------------------------------------------------------------------------------------------
std::string Quoted(std::string_view text)
{
    static const char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(text.size() + 2);
    result += '"';
    for (const char c : text)
    {
        const unsigned char byte = static_cast<unsigned char>(c);
        switch (c)
        {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (byte < 0x20 || byte == 0x7f)
            {
                result += "\\u00";
                result += kHex[byte >> 4];
                result += kHex[byte & 0xf];
            }
            else
            {
                result += c;
            }
            break;
        }
    }
    result += '"';
    return result;
}

} // namespace CpkgTool
