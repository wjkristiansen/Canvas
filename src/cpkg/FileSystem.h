//================================================================================================
// FileSystem - the files cpkg reads, creates, and writes
//
// Every function takes std::filesystem paths, made from UTF-8 with PathOf, and returns a
// FileResult rather than reporting: each caller words its own diagnostic. Whether two names are
// one is decided by the file system holding them, by its own rules for names.
//================================================================================================

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace CpkgTool
{

// The path a UTF-8 path names.
std::filesystem::path PathOf(std::string_view utf8);

// The UTF-8 form of path, with '/' separators.
std::string Utf8Of(const std::filesystem::path &path);

enum class FileResult
{
    Success,
    NotFound,   // the path names no file; for a read, nothing or something other than a file
    Exists,     // the file system already holds a file or directory of that name
    Failed,
};

// Reads the whole file at path into *pBytes.
FileResult ReadWholeFile(const std::filesystem::path &path, std::string *pBytes);

// Reads size bytes at offset of the file at path into *pBytes. Fails when the file ends before
// them.
FileResult ReadFileRange(const std::filesystem::path &path, uint64_t offset, uint64_t size, std::string *pBytes);

// Finds the size of the file at path.
FileResult FileSize(const std::filesystem::path &path, uint64_t *pSize);

// Writes bytes as the file at path, replacing any file there. A failure may leave part of the
// bytes written; CStagedFile writes a file that is either replaced whole or untouched.
FileResult WriteWholeFile(const std::filesystem::path &path, std::string_view bytes);

// Whether the files at a and b both exist and hold the same bytes.
bool SameBytes(const std::filesystem::path &a, const std::filesystem::path &b);

// Creates path as an empty file, unless the file system already holds a file or directory of
// that name.
FileResult MakeFile(const std::filesystem::path &path);

// Creates path as an empty directory, unless the file system already holds a file or directory
// of that name.
FileResult MakeDirectory(const std::filesystem::path &path);

// Removes the file at path, or the directory at path and everything in it. Removing what does
// not exist succeeds.
FileResult Remove(const std::filesystem::path &path);

// A file staged beside a path: what is written goes to a temporary file there, and Commit moves
// it over the file at the path, replacing or creating that file all at once. Until then the file
// at the path is as it was, and destroying an uncommitted CStagedFile removes the staged file.
class CStagedFile
{
public:
    explicit CStagedFile(std::filesystem::path path);
    ~CStagedFile();
    CStagedFile(const CStagedFile &) = delete;
    CStagedFile &operator=(const CStagedFile &) = delete;

    const std::filesystem::path &GetPath() const { return m_Path; }

    // The staged file, for a writer that opens a file by path to write it.
    const std::filesystem::path &GetStagedPath() const { return m_Staged; }

    // Writes bytes as the staged file.
    FileResult Write(std::string_view bytes);

    // Copies the file at source as the staged file.
    FileResult CopyFrom(const std::filesystem::path &source);

    // Moves the staged file over the file at the path, unless that already holds the same
    // bytes, which leaves it untouched.
    FileResult Commit();

private:
    std::filesystem::path m_Path;
    std::filesystem::path m_Staged;
};

// Replaces the file at path with bytes through a CStagedFile, unless it already holds them.
FileResult SaveFile(const std::filesystem::path &path, std::string_view bytes);

// A new directory beside a path, named for it and unique to this process, removed with
// everything in it when the CScopedDirectory is destroyed. The directories that hold it are
// created as needed, and stay.
class CScopedDirectory
{
public:
    explicit CScopedDirectory(const std::filesystem::path &beside);
    ~CScopedDirectory();
    CScopedDirectory(const CScopedDirectory &) = delete;
    CScopedDirectory &operator=(const CScopedDirectory &) = delete;

    // Whether the directory was created.
    bool IsCreated() const { return m_Created; }

    const std::filesystem::path &GetPath() const { return m_Path; }

private:
    std::filesystem::path m_Path;
    bool m_Created = false;
};

} // namespace CpkgTool
