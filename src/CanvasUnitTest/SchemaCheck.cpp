//================================================================================================
// SchemaCheck - Checks JSON files against the Canvas JSON schemas by running jsonschema-cli
//================================================================================================

#include "pch.h"
#include "SchemaCheck.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>

namespace
{
    std::filesystem::path PathFromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t *>(text.data()), text.size())).make_preferred();
    }

    std::filesystem::path SchemaPath(CanvasSchema schema)
    {
        return PathFromUtf8(schema == CanvasSchema::PackageSource ? CANVAS_PACKAGE_SOURCE_SCHEMA : CANVAS_PROJECT_SCHEMA);
    }

    std::wstring Quoted(const std::filesystem::path &path)
    {
        return L"\"" + path.wstring() + L"\"";
    }

    // Runs commandLine and collects what it writes to its standard output and error.
    bool Run(std::wstring commandLine, std::string *pOutput, DWORD *pExitCode)
    {
        SECURITY_ATTRIBUTES attributes = { sizeof(attributes), nullptr, TRUE };
        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &attributes, 0))
            return false;
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startup = { sizeof(startup) };
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = writePipe;
        startup.hStdError = writePipe;
        PROCESS_INFORMATION process = {};
        const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process);
        CloseHandle(writePipe);
        if (!started)
        {
            CloseHandle(readPipe);
            return false;
        }

        char buffer[4096];
        DWORD bytesRead = 0;
        while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead != 0)
            pOutput->append(buffer, bytesRead);
        CloseHandle(readPipe);

        WaitForSingleObject(process.hProcess, INFINITE);
        GetExitCodeProcess(process.hProcess, pExitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    struct Validation
    {
        bool Valid = false;
        std::string Report;     // each place the file breaks the schema, one per line
    };

    // Validates the file at path, or returns a failure describing why jsonschema-cli could not.
    ::testing::AssertionResult Validate(CanvasSchema schema, const std::filesystem::path &path, Validation *pValidation)
    {
        const std::wstring commandLine = Quoted(PathFromUtf8(CANVAS_JSONSCHEMA_CLI_EXE))
            + L" validate --draft 2020 --offline --output list " + Quoted(SchemaPath(schema)) + L" -i " + Quoted(path);
        std::string output;
        DWORD exitCode = 0;
        if (!Run(commandLine, &output, &exitCode))
            return ::testing::AssertionFailure() << "cannot run jsonschema-cli: " << CANVAS_JSONSCHEMA_CLI_EXE;

        // The list output is one JSON object per instance; any other output reports an error.
        const nlohmann::json result = nlohmann::json::parse(output, nullptr, false);
        if (result.is_discarded() || !result.contains("payload"))
            return ::testing::AssertionFailure() << "jsonschema-cli failed (exit code " << exitCode << "): " << output;

        const nlohmann::json &payload = result["payload"];
        pValidation->Valid = payload.value("valid", false);
        for (const nlohmann::json &detail : payload.value("details", nlohmann::json::array()))
        {
            if (!detail.contains("errors"))
                continue;
            for (const auto &[keyword, message] : detail["errors"].items())
                pValidation->Report += "  [" + detail.value("instanceLocation", std::string()) + "] " + message.get<std::string>() + "\n";
        }
        return ::testing::AssertionSuccess();
    }

    // Writes text to a file of its own in the temp directory, and removes the file when done.
    class CTempJsonFile
    {
    public:
        explicit CTempJsonFile(std::string_view text)
        {
            static std::atomic<unsigned> s_Count = 0;
            m_Path = std::filesystem::temp_directory_path()
                / ("CanvasSchemaCheck_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(s_Count++) + ".json");
            std::ofstream file(m_Path, std::ios::binary);
            file.write(text.data(), std::streamsize(text.size()));
        }

        ~CTempJsonFile()
        {
            std::error_code error;
            std::filesystem::remove(m_Path, error);
        }

        const std::filesystem::path &Path() const { return m_Path; }

    private:
        std::filesystem::path m_Path;
    };
}

bool SchemaChecksEnabled()
{
    const bool enabled = std::string_view(CANVAS_JSONSCHEMA_CLI_EXE).size() != 0;
    static bool s_Noted = false;
    if (!enabled && !s_Noted)
    {
        s_Noted = true;
        std::printf("[   NOTE   ] jsonschema-cli is not available, so the schema checks are skipped\n");
    }
    return enabled;
}

::testing::AssertionResult MatchesSchema(CanvasSchema schema, const std::filesystem::path &path)
{
    if (!SchemaChecksEnabled())
        return ::testing::AssertionSuccess();
    Validation validation;
    if (::testing::AssertionResult ran = Validate(schema, path, &validation); !ran)
        return ran;
    if (validation.Valid)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << path.string() << " breaks " << SchemaPath(schema).filename().string() << ":\n"
        << validation.Report;
}

::testing::AssertionResult TextMatchesSchema(CanvasSchema schema, std::string_view text)
{
    if (!SchemaChecksEnabled())
        return ::testing::AssertionSuccess();
    const CTempJsonFile file(text);
    if (::testing::AssertionResult result = MatchesSchema(schema, file.Path()); !result)
        return ::testing::AssertionFailure() << result.message() << "in:\n" << text;
    return ::testing::AssertionSuccess();
}

::testing::AssertionResult TextBreaksSchema(CanvasSchema schema, std::string_view text)
{
    if (!SchemaChecksEnabled())
        return ::testing::AssertionSuccess();
    const CTempJsonFile file(text);
    Validation validation;
    if (::testing::AssertionResult ran = Validate(schema, file.Path(), &validation); !ran)
        return ran;
    if (!validation.Valid)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << "matches " << SchemaPath(schema).filename().string() << ":\n" << text;
}
