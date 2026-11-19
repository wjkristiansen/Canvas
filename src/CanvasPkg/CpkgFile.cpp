//================================================================================================
// CpkgFile
//================================================================================================

#include "CpkgFile.h"

#include <algorithm>
#include <iterator>
#include <new>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#else
  #include <cerrno>
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace Canvas
{

#if defined(_WIN32)

namespace
{
    // A UTF-8 path converted to the UTF-16 form the -W functions take. A path that fits the
    // inline buffer converts without allocating.
    class CWidePath
    {
    public:
        CWidePath() = default;
        CWidePath(const CWidePath &) = delete;
        CWidePath &operator=(const CWidePath &) = delete;

        // Returns false if fileName is not valid UTF-8. Throws std::bad_alloc.
        bool Convert(const char *fileName)
        {
            int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName, -1, m_Inline, int(std::size(m_Inline)));
            if (length > 0)
            {
                m_pPath = m_Inline;
                return true;
            }
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                return false;

            length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName, -1, nullptr, 0);
            if (length <= 0)
                return false;
            m_pHeap.reset(new wchar_t[size_t(length)]);
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName, -1, m_pHeap.get(), length) != length)
                return false;
            m_pPath = m_pHeap.get();
            return true;
        }

        const wchar_t *Get() const { return m_pPath; }

    private:
        wchar_t m_Inline[MAX_PATH];
        std::unique_ptr<wchar_t[]> m_pHeap;
        const wchar_t *m_pPath = nullptr;
    };

    // Windows transfers at most a DWORD per call; keep each call well within it.
    constexpr size_t kMaxTransfer = 1u << 30;

    OVERLAPPED OffsetOverlapped(uint64_t offset)
    {
        OVERLAPPED overlapped = {};
        overlapped.Offset = DWORD(offset & 0xffffffffu);
        overlapped.OffsetHigh = DWORD(offset >> 32);
        return overlapped;
    }
}

CCpkgFile::~CCpkgFile()
{
    CloseHandle(m_Handle);
}

Gem::Result CCpkgFile::CreateNew(const char *fileName, std::unique_ptr<CCpkgFile> *ppFile)
{
    try
    {
        CWidePath path;
        if (!path.Convert(fileName))
            return Gem::Result::IoError;

        HANDLE handle = CreateFileW(path.Get(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            return (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) ? Gem::Result::AlreadyExists : Gem::Result::IoError;
        }
        try
        {
            ppFile->reset(new CCpkgFile(handle));
        }
        catch (const std::bad_alloc &)
        {
            CloseHandle(handle);
            DeleteFileW(path.Get());
            throw;
        }
        return Gem::Result::Success;
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
}

Gem::Result CCpkgFile::OpenRead(const char *fileName, std::unique_ptr<CCpkgFile> *ppFile)
{
    try
    {
        CWidePath path;
        if (!path.Convert(fileName))
            return Gem::Result::NotFound;

        HANDLE handle = CreateFileW(path.Get(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? Gem::Result::NotFound : Gem::Result::IoError;
        }
        try
        {
            ppFile->reset(new CCpkgFile(handle));
        }
        catch (const std::bad_alloc &)
        {
            CloseHandle(handle);
            throw;
        }
        return Gem::Result::Success;
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
}

void CCpkgFile::Delete(const char *fileName)
{
    try
    {
        CWidePath path;
        if (path.Convert(fileName))
            DeleteFileW(path.Get());
    }
    catch (const std::bad_alloc &)
    {
    }
}

Gem::Result CCpkgFile::GetSize(uint64_t *pSize) const
{
    LARGE_INTEGER size;
    if (!GetFileSizeEx(m_Handle, &size))
        return Gem::Result::IoError;
    *pSize = uint64_t(size.QuadPart);
    return Gem::Result::Success;
}

Gem::Result CCpkgFile::ReadAt(uint64_t offset, void *pData, size_t size) const
{
    auto *pBytes = static_cast<std::byte *>(pData);
    while (size > 0)
    {
        const DWORD request = DWORD(std::min(size, kMaxTransfer));
        OVERLAPPED overlapped = OffsetOverlapped(offset);
        DWORD read = 0;
        if (!ReadFile(m_Handle, pBytes, request, &read, &overlapped) || read != request)
            return Gem::Result::IoError;
        pBytes += read;
        offset += read;
        size -= read;
    }
    return Gem::Result::Success;
}

Gem::Result CCpkgFile::WriteAt(uint64_t offset, const void *pData, size_t size)
{
    auto *pBytes = static_cast<const std::byte *>(pData);
    while (size > 0)
    {
        const DWORD request = DWORD(std::min(size, kMaxTransfer));
        OVERLAPPED overlapped = OffsetOverlapped(offset);
        DWORD written = 0;
        if (!WriteFile(m_Handle, pBytes, request, &written, &overlapped) || written != request)
            return Gem::Result::IoError;
        pBytes += written;
        offset += written;
        size -= written;
    }
    return Gem::Result::Success;
}

#else // POSIX

CCpkgFile::~CCpkgFile()
{
    close(m_Fd);
}

Gem::Result CCpkgFile::CreateNew(const char *fileName, std::unique_ptr<CCpkgFile> *ppFile)
{
    const int fd = open(fileName, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0)
        return errno == EEXIST ? Gem::Result::AlreadyExists : Gem::Result::IoError;
    try
    {
        ppFile->reset(new CCpkgFile(fd));
    }
    catch (const std::bad_alloc &)
    {
        close(fd);
        unlink(fileName);
        return Gem::Result::OutOfMemory;
    }
    return Gem::Result::Success;
}

Gem::Result CCpkgFile::OpenRead(const char *fileName, std::unique_ptr<CCpkgFile> *ppFile)
{
    const int fd = open(fileName, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return (errno == ENOENT || errno == ENOTDIR) ? Gem::Result::NotFound : Gem::Result::IoError;
    try
    {
        ppFile->reset(new CCpkgFile(fd));
    }
    catch (const std::bad_alloc &)
    {
        close(fd);
        return Gem::Result::OutOfMemory;
    }
    return Gem::Result::Success;
}

void CCpkgFile::Delete(const char *fileName)
{
    unlink(fileName);
}

Gem::Result CCpkgFile::GetSize(uint64_t *pSize) const
{
    struct stat info;
    if (fstat(m_Fd, &info) != 0)
        return Gem::Result::IoError;
    *pSize = uint64_t(info.st_size);
    return Gem::Result::Success;
}

Gem::Result CCpkgFile::ReadAt(uint64_t offset, void *pData, size_t size) const
{
    auto *pBytes = static_cast<std::byte *>(pData);
    while (size > 0)
    {
        const ssize_t read = pread(m_Fd, pBytes, size, off_t(offset));
        if (read < 0 && errno == EINTR)
            continue;
        if (read <= 0)
            return Gem::Result::IoError;
        pBytes += read;
        offset += uint64_t(read);
        size -= size_t(read);
    }
    return Gem::Result::Success;
}

Gem::Result CCpkgFile::WriteAt(uint64_t offset, const void *pData, size_t size)
{
    auto *pBytes = static_cast<const std::byte *>(pData);
    while (size > 0)
    {
        const ssize_t written = pwrite(m_Fd, pBytes, size, off_t(offset));
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return Gem::Result::IoError;
        pBytes += written;
        offset += uint64_t(written);
        size -= size_t(written);
    }
    return Gem::Result::Success;
}

#endif

} // namespace Canvas
