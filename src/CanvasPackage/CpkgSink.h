//================================================================================================
// CCpkgSink - a buffered, seekable byte sink for writing a .cpkg container to a file.
//
// The write counterpart to CCpkgSource. Bulk payloads stream straight to disk through an internal
// flush cache, so a large package is never fully resident: only the bounded header + chunk table
// is composed up front, and the table entries are back-patched at finalize via PatchBytes.
//
// TODO: The cache size is tunable (CreateFile's flushBufferSize). A background drain
// thread could be folded in behind this same interface later without changing callers.
//
// Error model: the streaming append helpers (WriteBytes and its typed wrappers, PadToAlignment)
// throw a CpkgError carrying the precise Gem::Result (see CpkgLog.h) on the first failure, so a long
// run of writes needs no per-call result checks; the public API boundary catches the exception and
// returns its result. The low-frequency calls - CreateFile, PatchBytes, Flush, and Close - return
// a Gem::Result directly, where a single check at the call site costs nothing. CreateFile mirrors
// CCpkgSource::OpenFile. Internal to CanvasPackage; not part of the public Inc/ surface.
//================================================================================================
#pragma once

#include "CanvasPackageData.h" // Canvas::PackageLogFn, Gem::Result

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <vector>

namespace Canvas::Cpkg
{

class CCpkgSink
{
public:
    static constexpr size_t kDefaultFlushBufferSize = 256 * 1024;

    CCpkgSink() = default; // an empty sink; writes throw Uninitialized until CreateFile succeeds
    ~CCpkgSink();          // flushes and closes a file backing (best effort; check Close() to be sure)

    CCpkgSink(const CCpkgSink&)            = delete;
    CCpkgSink& operator=(const CCpkgSink&) = delete;

    // Create (truncate) a .cpkg file for streaming writes. flushBufferSize sets the append-cache
    // block size; logFn (retained for the sink's lifetime) reports the first I/O failure. Logs and
    // fails if the file cannot be opened for writing.
    static Gem::Result CreateFile(const char* pFilePath, CCpkgSink* pOut,
                                  size_t flushBufferSize = kDefaultFlushBufferSize,
                                  const PackageLogFn& logFn = {});

    // Append helpers. Each copies into the flush cache (block-flushing to disk when it fills) and
    // throws CpkgError on an I/O failure or an unopened sink.
    void WriteBytes(const void* data, size_t count);
    void WriteU8(uint8_t v)   { WriteBytes(&v, sizeof v); }
    void WriteU16(uint16_t v) { WriteBytes(&v, sizeof v); }
    void WriteU32(uint32_t v) { WriteBytes(&v, sizeof v); }
    void WriteU64(uint64_t v) { WriteBytes(&v, sizeof v); }
    void WriteI32(int32_t v)  { WriteBytes(&v, sizeof v); }
    void WriteFloat(float v)  { WriteBytes(&v, sizeof v); }
    void WriteFloats(const float* data, size_t count)  { WriteBytes(data, count * sizeof(float)); }
    void WriteU32s(const uint32_t* data, size_t count) { WriteBytes(data, count * sizeof(uint32_t)); }
    void WriteI32s(const int32_t* data, size_t count)  { WriteBytes(data, count * sizeof(int32_t)); }

    // Append zero bytes until Tell() is a multiple of alignment.
    void PadToAlignment(size_t alignment);

    // Back-patch already-produced bytes at an absolute file offset (the [offset, offset+size) range
    // must fall within what has been written so far). Flushes the append cache, seeks, writes, and
    // restores the append position to the end. Used to fill chunk-table entries at finalize.
    Gem::Result PatchBytes(uint64_t offset, const void* data, size_t size);

    // Absolute append offset = total bytes written so far (flushed + cached).
    uint64_t Tell() const { return m_Written; }

    // Flush the append cache to the file. Returns the resulting status.
    Gem::Result Flush();

    // Flush and close the file. Idempotent. Returns the resulting status.
    Gem::Result Close();

private:
    Gem::Result FlushBuffer(); // write the cached bytes to the stream; logs and returns Fail on failure

    enum class Backing { Empty, File };

    Backing              m_Backing    = Backing::Empty;
    std::ofstream        m_Stream;                          // File backing
    std::vector<uint8_t> m_Buffer;                          // append cache; size() is its capacity
    size_t               m_BufferUsed = 0;                  // bytes currently cached
    uint64_t             m_Written    = 0;                  // total appended (flushed + cached)
    PackageLogFn         m_LogFn;                           // retained sink for I/O failure reports
};

} // namespace Canvas::Cpkg
