//================================================================================================
// CpkgTestUtil - shared helpers for the .cpkg I/O tests now that writes go to real files.
//================================================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <vector>

namespace CanvasUnitTest
{

// A unique temp file path, removed on construction and destruction, so each test starts and leaves
// clean even after an aborted prior run.
struct TempFile
{
    explicit TempFile(const wchar_t* name)
        : path(std::filesystem::temp_directory_path() / name)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::wstring wstr() const { return path.wstring(); }
    std::filesystem::path path;
};

// Slurp an entire file into a byte vector (used to inspect / corrupt streamed output in tests).
inline std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamoff size = f.tellg();
    std::vector<uint8_t> bytes(size > 0 ? static_cast<size_t>(size) : 0);
    f.seekg(0, std::ios::beg);
    if (!bytes.empty())
        f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

} // namespace CanvasUnitTest
