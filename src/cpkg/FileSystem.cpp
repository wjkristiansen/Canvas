//================================================================================================
// FileSystem - the files cpkg reads, creates, and writes
//================================================================================================

#include "FileSystem.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace CpkgTool
{

namespace fs = std::filesystem;

namespace
{
    // The result of an attempt to create path, which created it when created.
    FileResult MadeOrWhyNot(const fs::path &path, bool created)
    {
        if (created)
            return FileResult::Success;
        std::error_code error;
        return Exists(path) ? FileResult::Exists : FileResult::Failed;
    }

    // NotFound when path names nothing or something other than a file, or else Success.
    FileResult FindFile(const fs::path &path)
    {
        std::error_code error;
        return fs::is_regular_file(path, error) ? FileResult::Success : FileResult::NotFound;
    }

    // A name beside path, named for it and unique to this process.
    fs::path TemporaryPath(const fs::path &path)
    {
#if defined(_WIN32)
        const int process = _getpid();
#else
        const int process = int(getpid());
#endif
        fs::path temporary = path;
        temporary += "." + std::to_string(process) + ".tmp";
        return temporary;
    }
}

//------------------------------------------------------------------------------------------------
fs::path PathOf(std::string_view utf8)
{
    return fs::path(std::u8string_view(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

std::string Utf8Of(const fs::path &path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

//------------------------------------------------------------------------------------------------
FileResult ReadWholeFile(const fs::path &path, std::string *pBytes)
{
    if (const FileResult found = FindFile(path); found != FileResult::Success)
        return found;
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return FileResult::Failed;
    pBytes->assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return stream.bad() ? FileResult::Failed : FileResult::Success;
}

FileResult ReadFileRange(const fs::path &path, uint64_t offset, uint64_t size, std::string *pBytes)
{
    if (const FileResult found = FindFile(path); found != FileResult::Success)
        return found;
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.seekg(std::streamoff(offset)))
        return FileResult::Failed;
    pBytes->resize(size_t(size));
    return stream.read(pBytes->data(), std::streamsize(size)) ? FileResult::Success : FileResult::Failed;
}

FileResult FileSize(const fs::path &path, uint64_t *pSize)
{
    if (const FileResult found = FindFile(path); found != FileResult::Success)
        return found;
    std::error_code error;
    *pSize = fs::file_size(path, error);
    return error ? FileResult::Failed : FileResult::Success;
}

FileResult WriteWholeFile(const fs::path &path, std::string_view bytes)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), std::streamsize(bytes.size()));
    stream.close();
    return stream ? FileResult::Success : FileResult::Failed;
}

FileResult FileStamp(const fs::path &path, uint64_t *pSize, int64_t *pTime)
{
    if (const FileResult size = FileSize(path, pSize); size != FileResult::Success)
        return size;
    std::error_code error;
    const fs::file_time_type time = fs::last_write_time(path, error);
    if (error)
        return FileResult::Failed;
    *pTime = std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
    return FileResult::Success;
}

bool Exists(const fs::path &path)
{
    std::error_code error;
    return fs::exists(fs::symlink_status(path, error));
}

bool SameFile(const fs::path &a, const fs::path &b)
{
    std::error_code error;
    return fs::equivalent(a, b, error);
}

bool SameBytes(const fs::path &a, const fs::path &b)
{
    std::error_code error;
    const uintmax_t size = fs::file_size(a, error);
    if (error || fs::file_size(b, error) != size || error)
        return false;

    std::ifstream fileA(a, std::ios::binary);
    std::ifstream fileB(b, std::ios::binary);
    char bufferA[64 * 1024];
    char bufferB[64 * 1024];
    while (fileA && fileB)
    {
        fileA.read(bufferA, sizeof(bufferA));
        fileB.read(bufferB, sizeof(bufferB));
        if (fileA.gcount() != fileB.gcount() || std::memcmp(bufferA, bufferB, size_t(fileA.gcount())) != 0)
            return false;
    }
    return fileA.eof() && fileB.eof();
}

//------------------------------------------------------------------------------------------------
FileResult MakeFile(const fs::path &path)
{
#if defined(_WIN32)
    const int file = _wopen(path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
    return MadeOrWhyNot(path, file != -1 && _close(file) == 0);
#else
    const int file = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
    return MadeOrWhyNot(path, file != -1 && close(file) == 0);
#endif
}

FileResult MakeDirectory(const fs::path &path)
{
    std::error_code error;
    return MadeOrWhyNot(path, fs::create_directory(path, error) && !error);
}

FileResult MakeDirectories(const fs::path &path, fs::path *pOutermost)
{
    std::error_code error;
    if (pOutermost)
    {
        pOutermost->clear();
        for (fs::path at = fs::absolute(path, error); !error && !Exists(at) && at != at.parent_path(); at = at.parent_path())
            *pOutermost = at;
    }
    fs::create_directories(path, error);
    if (error)
        return FileResult::Failed;
    return fs::is_directory(path, error) ? FileResult::Success : FileResult::Exists;
}

FileResult Rename(const fs::path &from, const fs::path &to)
{
    std::error_code error;
    fs::rename(from, to, error);
    return error ? FileResult::Failed : FileResult::Success;
}

FileResult Remove(const fs::path &path)
{
    std::error_code error;
    fs::remove_all(path, error);
    return error ? FileResult::Failed : FileResult::Success;
}

//------------------------------------------------------------------------------------------------
CStagedFile::CStagedFile(fs::path path) :
    m_Path(std::move(path)),
    m_Staged(TemporaryPath(m_Path))
{
    // A temporary file an earlier run of this process left.
    std::error_code error;
    fs::remove(m_Staged, error);
}

CStagedFile::~CStagedFile()
{
    std::error_code error;
    fs::remove(m_Staged, error);
}

FileResult CStagedFile::Write(std::string_view bytes)
{
    return WriteWholeFile(m_Staged, bytes);
}

FileResult CStagedFile::CopyFrom(const fs::path &source)
{
    if (const FileResult found = FindFile(source); found != FileResult::Success)
        return found;
    std::error_code error;
    fs::copy_file(source, m_Staged, fs::copy_options::overwrite_existing, error);
    return error ? FileResult::Failed : FileResult::Success;
}

FileResult CStagedFile::Commit()
{
    if (SameBytes(m_Staged, m_Path))
        return FileResult::Success;
    std::error_code error;
    return Rename(m_Staged, m_Path);
}

FileResult SaveFile(const fs::path &path, std::string_view bytes)
{
    CStagedFile file(path);
    const FileResult written = file.Write(bytes);
    return written == FileResult::Success ? file.Commit() : written;
}

//------------------------------------------------------------------------------------------------
CScopedDirectory::CScopedDirectory(const fs::path &beside) :
    m_Path(TemporaryPath(beside))
{
    Remove(m_Path);
    m_Created = MakeDirectory(m_Path) == FileResult::Success;
}

CScopedDirectory::~CScopedDirectory()
{
    if (m_Created)
        Remove(m_Path);
}

} // namespace CpkgTool
