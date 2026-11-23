//================================================================================================
// Diagnostics
//================================================================================================

#include "Diagnostics.h"

#include <filesystem>
#include <string>
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
void CDiagnostics::Emit(Severity severity, std::string_view file, std::string_view location, std::string_view text)
{
    if (severity == Severity::Warning && m_pWarningLog)
        m_pWarningLog->push_back(AbsolutePath(file) + std::string(location) + ": warning: " + std::string(text));
    if (severity == Severity::Warning && m_Werror)
        severity = Severity::Error;
    if (severity == Severity::Error)
        ++m_ErrorCount;
    if (severity == Severity::Error || !m_Quiet)
        m_Stream << DisplayPath(file) << location << ": " << SeverityName(severity) << ": " << text << '\n';
}

//------------------------------------------------------------------------------------------------
void CDiagnostics::Report(Severity severity, std::string_view file, std::string_view message)
{
    Emit(severity, file, {}, message);
}

//------------------------------------------------------------------------------------------------
void CDiagnostics::Report(Severity severity, std::string_view file, uint32_t line, uint32_t column, std::string_view message, std::string_view pointer)
{
    const std::string location = ':' + std::to_string(line) + ':' + std::to_string(column);
    Emit(severity, file, location, std::string(message) + " [" + std::string(pointer) + "]");
}

//------------------------------------------------------------------------------------------------
void CDiagnostics::Report(Severity severity, std::string_view file, uint32_t line, uint32_t column, std::string_view message)
{
    const std::string location = ':' + std::to_string(line) + ':' + std::to_string(column);
    Emit(severity, file, location, message);
}

//------------------------------------------------------------------------------------------------
bool CDiagnostics::ReportLogged(std::string_view file, std::string_view logged)
{
    constexpr std::string_view kSeverity = ": warning: ";
    if (!logged.starts_with(file))
        return false;
    const std::string_view rest = logged.substr(file.size());
    const size_t at = rest.find(kSeverity);
    if (at == std::string_view::npos || (at != 0 && rest[0] != ':'))
        return false;
    Emit(Severity::Warning, file, rest.substr(0, at), rest.substr(at + kSeverity.size()));
    return true;
}

//------------------------------------------------------------------------------------------------
void CDiagnostics::Append(std::string_view text, size_t errorCount)
{
    m_Stream << text;
    m_ErrorCount += errorCount;
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
std::string AbsolutePath(std::string_view path)
{
    namespace fs = std::filesystem;
    const fs::path given(std::u8string_view(reinterpret_cast<const char8_t *>(path.data()), path.size()));
    std::error_code ec;
    fs::path absolute = fs::absolute(given, ec);
    if (ec)
        absolute = given;
    const std::u8string utf8 = absolute.lexically_normal().generic_u8string();
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
