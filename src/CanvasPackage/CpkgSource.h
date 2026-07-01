//================================================================================================
// CCpkgSource - a seekable byte source for a .cpkg container file.
//
// The container layer reads the header, chunk table, and individual chunk / subresource byte
// ranges through a CCpkgSource, so a large package never has to be fully resident: the file-backed
// source streams ranges on demand.
//
// One concrete class rather than an abstract base plus derived classes -- this is a static library
// and the backing set is small and closed. A platform-optimized backing (overlapped I/O,
// DirectStorage) can be folded in here later without changing callers.
//================================================================================================
#pragma once

#include "CanvasPackageData.h" // Canvas::PackageLogFn, Gem::Result

#include <cstddef>
#include <cstdint>
#include <fstream>

namespace Canvas::Cpkg
{

class CCpkgSource
{
public:
    CCpkgSource() = default; // an empty source (Size() == 0); every Read fails

    // Open a .cpkg file for streaming reads. Logs and fails if the file cannot be opened.
    static Gem::Result OpenFile(const wchar_t* pFilePath, CCpkgSource* pOut,
                                const PackageLogFn& logFn = {});

    // Read exactly 'size' bytes starting at absolute 'offset' into 'dst'. A zero-size read at any
    // in-range offset succeeds. Returns InvalidArg for an out-of-range request, Uninitialized for an
    // empty (default-constructed) source, and Fail for a short read from an in-range file request.
    Gem::Result Read(uint64_t offset, void* dst, size_t size);

    uint64_t Size() const { return m_Size; }

private:
    enum class Backing { Empty, File };

    Backing       m_Backing = Backing::Empty;
    std::ifstream m_Stream;             // File backing
    uint64_t      m_Size     = 0;
};

} // namespace Canvas::Cpkg
