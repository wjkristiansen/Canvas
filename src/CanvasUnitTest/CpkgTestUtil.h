//================================================================================================
// CpkgTestUtil - shared helpers for the .cpkg I/O tests now that writes go to real files.
//================================================================================================
#pragma once

#include "CpkgLog.h" // Canvas::Cpkg::CpkgError

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace CanvasUnitTest
{

// A unique temp file path, removed on construction and destruction, so each test starts and leaves
// clean even after an aborted prior run.
struct TempFile
{
    explicit TempFile(const char* name)
        : path(std::filesystem::temp_directory_path() / std::filesystem::u8path(name))
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    std::string str() const { return path.u8string(); }
    std::filesystem::path path;
};

// Run write-path code that must fail and assert it throws a CpkgError carrying the expected
// result.
template <typename Fn>
void ExpectCpkgError(Gem::Result expected, Fn&& fn)
{
    try
    {
        std::forward<Fn>(fn)();
        ADD_FAILURE() << "expected CpkgError(" << Gem::GemResultString(expected)
                      << ") was not thrown";
    }
    catch (const Canvas::Cpkg::CpkgError& e)
    {
        EXPECT_EQ(e.Result(), expected) << e.what();
    }
}

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
