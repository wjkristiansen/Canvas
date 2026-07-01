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
// Error model: the append helpers return void and latch the first failure into a sticky status
// (like std::ostream's failbit). Once latched, further appends are no-ops, so callers issue a long
// run of writes and check the outcome once via Status(), Flush(), or Close(), each of which returns
// the precise Gem::Result. PatchBytes (the cold finalize path) returns its Result directly. Internal
// to CanvasPackage; not part of the public Inc/ surface.
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

    CCpkgSink() = default; // an empty sink; every write is a no-op until CreateFile succeeds
    ~CCpkgSink();          // flushes and closes a file backing (best effort; check Close() to be sure)

    CCpkgSink(const CCpkgSink&)            = delete;
    CCpkgSink& operator=(const CCpkgSink&) = delete;

    // Create (truncate) a .cpkg file for streaming writes. flushBufferSize sets the append-cache
    // block size; logFn (retained for the sink's lifetime) reports the first I/O failure. Logs and
    // fails if the file cannot be opened for writing.
    static Gem::Result CreateFile(const wchar_t* pFilePath, CCpkgSink* pOut,
                                  size_t flushBufferSize = kDefaultFlushBufferSize,
                                  const PackageLogFn& logFn = {});

    // Append helpers. Each copies into the flush cache (block-flushing when full) and latches the
    // first failure; no-ops once the sink is in a failed state. Check Status()/Flush()/Close().
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

    // The sticky status: Success until the first failure latches a precise code.
    Gem::Result Status() const { return m_Status; }

    // Flush the append cache to the file. Returns the sticky status.
    Gem::Result Flush();

    // Flush and close the file. Idempotent. Returns the sticky status.
    Gem::Result Close();

private:
    void FlushBuffer();          // write the cached bytes to the stream; latches on failure
    void Latch(Gem::Result r);   // record the first failing status; ignored once already failed

    enum class Backing { Empty, File };

    Backing              m_Backing    = Backing::Empty;
    std::ofstream        m_Stream;                          // File backing
    std::vector<uint8_t> m_Buffer;                          // append cache; size() is its capacity
    size_t               m_BufferUsed = 0;                  // bytes currently cached
    uint64_t             m_Written    = 0;                  // total appended (flushed + cached)
    Gem::Result          m_Status     = Gem::Result::Success;
    PackageLogFn         m_LogFn;                           // retained sink for I/O failure reports
};

} // namespace Canvas::Cpkg
