//================================================================================================
// CpkgIO - the .cpkg container layer: CRC32, header and chunk-table read/write, and the chunk-table
// back-patch the streaming writer relies on.
//
// Internal to CanvasPackage. The write path streams to a CCpkgSink; the read path parses a byte
// block via CCpkgReader or pulls header / chunk-table ranges from a CCpkgSource.
//
// Error policy: the read path fails fast and returns a Gem::Result, logging the precise reason for
// any rejection through the caller-supplied PackageLogFn. The write helpers append to a CCpkgSink,
// which latches the first failure into its sticky status; they therefore return void (the bounded
// header + table is composed up front and back-patched at finalize), and the caller checks the
// outcome once via CCpkgSink::Status() / Close().
//================================================================================================
#pragma once

#include "CanvasPackageData.h" // Canvas::PackageLogFn / PackageLogLevel, Gem::Result
#include "CpkgBlobTypes.h"
#include "CpkgSink.h"
#include "CpkgSource.h"
#include "CpkgStream.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Canvas::Cpkg
{

//--------------------------------------------------------------------------------------------------
// CRC32 - standard CRC-32 (zlib / PKZIP) with reversed polynomial 0xEDB88320, initial value
// 0xFFFFFFFF, and final inversion. The ASCII string "123456789" hashes to 0xCBF43926.
//--------------------------------------------------------------------------------------------------
uint32_t CRC32(const uint8_t* data, size_t size);

//--------------------------------------------------------------------------------------------------
// Header write path. Appends to the sink; failures latch on the sink's sticky status.
//--------------------------------------------------------------------------------------------------

// Write the complete fixed-size header (CPKG_HEADER_SIZE bytes).
void WriteCpkgHeader(CCpkgSink& sink, uint32_t chunkCount);

//--------------------------------------------------------------------------------------------------
// Chunk-table write path. Appends to the sink; failures latch on the sink's sticky status.
//--------------------------------------------------------------------------------------------------

// Write chunkCount zeroed placeholder entries. The caller records sink.Tell() immediately before
// this call as the table-start offset for the PatchChunkEntry calls that follow.
void WriteChunkTable(CCpkgSink& sink, uint32_t chunkCount);

// Back-patch one chunk-table entry in place once its payload has been streamed and its offset/size
// are known. SizeCompressed is set equal to sizeRaw (v1 is uncompressed); the entry's Flags and
// ChunkGUID stay at their zeroed placeholder values. A patch failure latches the sink's status.
void PatchChunkEntry(CCpkgSink& sink, uint64_t tableOffset, uint32_t entryIndex,
                     uint32_t fourcc, uint16_t version, uint64_t dataOffset, uint32_t sizeRaw);

//--------------------------------------------------------------------------------------------------
// Read path. Fail-fast: each function stops at the first malformed value and logs the reason
// (offending value vs. expectation) through logFn before returning a failing Gem::Result.
//--------------------------------------------------------------------------------------------------

// Read and validate the header from the reader's current position, advancing the cursor past it.
// Fails on a null out pointer, a short buffer, a bad magic, a clear little-endian flag, or a header
// CRC mismatch.
Gem::Result ReadCpkgHeader(CCpkgReader& reader, CpkgHeaderData* out, const PackageLogFn& logFn = {});

// Read 'count' chunk-table entries from the reader's current position. Fails on a null out pointer
// or a buffer overrun.
Gem::Result ReadChunkTable(CCpkgReader& reader, uint32_t count, std::vector<ChunkEntryData>* out,
                           const PackageLogFn& logFn = {});

//--------------------------------------------------------------------------------------------------
// Source-based read path. These stream just the header / chunk-table byte ranges from a
// CCpkgSource (never the whole file) and delegate to the CCpkgReader overloads above. This is the
// entry point the streaming runtime loader uses.
//--------------------------------------------------------------------------------------------------

// Read and validate the header from the start of the source.
Gem::Result ReadCpkgHeader(CCpkgSource& source, CpkgHeaderData* out, const PackageLogFn& logFn = {});

// Read 'count' chunk-table entries from immediately after the header.
Gem::Result ReadChunkTable(CCpkgSource& source, uint32_t count, std::vector<ChunkEntryData>* out,
                           const PackageLogFn& logFn = {});

} // namespace Canvas::Cpkg
