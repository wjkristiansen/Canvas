//================================================================================================
// CpkgFile - positional file access for CCpkgWriter and CCpkgReader
//================================================================================================

#pragma once

#include <Gem.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Canvas
{

// One open file. Paths are UTF-8. Reads and writes take an explicit offset and share no seek
// cursor, so concurrent ReadAt calls on one file are safe.
class CCpkgFile
{
public:
    ~CCpkgFile();
    CCpkgFile(const CCpkgFile &) = delete;
    CCpkgFile &operator=(const CCpkgFile &) = delete;

    // Creates a file for write. Fails with AlreadyExists if it exists, IoError otherwise.
    static Gem::Result CreateNew(const char *fileName, std::unique_ptr<CCpkgFile> *ppFile);

    // Opens an existing file for read. Fails with NotFound if it does not exist, IoError otherwise.
    static Gem::Result OpenRead(const char *fileName, std::unique_ptr<CCpkgFile> *ppFile);

    // Deletes a file that is not open.
    static void Delete(const char *fileName);

    Gem::Result GetSize(uint64_t *pSize) const;

    // Reads exactly size bytes at offset; a short read is an IoError.
    Gem::Result ReadAt(uint64_t offset, void *pData, size_t size) const;

    // Writes exactly size bytes at offset.
    Gem::Result WriteAt(uint64_t offset, const void *pData, size_t size);

private:
#if defined(_WIN32)
    explicit CCpkgFile(void *handle) : m_Handle(handle) {}
    void *m_Handle;
#else
    explicit CCpkgFile(int fd) : m_Fd(fd) {}
    int m_Fd;
#endif
};

} // namespace Canvas
