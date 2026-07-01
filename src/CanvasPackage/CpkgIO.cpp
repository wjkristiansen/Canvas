#include "pch.h"
#include "CpkgIO.h"
#include "CpkgLog.h"

#include <array>
#include <cstring>

namespace Canvas::Cpkg
{

namespace
{
    // Build the 256-entry CRC-32 lookup table for polynomial 0xEDB88320 once, on first use.
    const std::array<uint32_t, 256>& Crc32Table()
    {
        static const std::array<uint32_t, 256> table = []
        {
            std::array<uint32_t, 256> t{};
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                t[i] = c;
            }
            return t;
        }();
        return table;
    }
}

uint32_t CRC32(const uint8_t* data, size_t size)
{
    const std::array<uint32_t, 256>& table = Crc32Table();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void WriteCpkgHeader(CCpkgSink& sink, uint32_t chunkCount)
{
    // Compose the header in a local image so its CRC can be computed before it is written. The host
    // is little-endian (the format asserts it), so scalars are copied as-is.
    uint8_t hdr[CPKG_HEADER_SIZE] = {};
    const uint32_t magic    = CPKG_MAGIC;
    const uint16_t vMajor   = CPKG_VERSION_MAJOR;
    const uint16_t vMinor   = CPKG_VERSION_MINOR;
    const uint32_t flags    = CPKG_FLAG_LITTLE_ENDIAN;
    const uint32_t reserved = 0u;
    std::memcpy(hdr + CPKG_HEADER_OFFSET_MAGIC,         &magic,      sizeof(magic));
    std::memcpy(hdr + CPKG_HEADER_OFFSET_VERSION_MAJOR, &vMajor,     sizeof(vMajor));
    std::memcpy(hdr + CPKG_HEADER_OFFSET_VERSION_MINOR, &vMinor,     sizeof(vMinor));
    std::memcpy(hdr + CPKG_HEADER_OFFSET_CHUNK_COUNT,   &chunkCount, sizeof(chunkCount));
    std::memcpy(hdr + CPKG_HEADER_OFFSET_FLAGS,         &flags,      sizeof(flags));
    std::memcpy(hdr + CPKG_HEADER_OFFSET_RESERVED,      &reserved,   sizeof(reserved));

    const uint32_t crc = CRC32(hdr, CPKG_HEADER_CRC_COVERAGE);
    std::memcpy(hdr + CPKG_HEADER_OFFSET_CRC, &crc, sizeof(crc));

    sink.WriteBytes(hdr, CPKG_HEADER_SIZE);
}

void WriteChunkTable(CCpkgSink& sink, uint32_t chunkCount)
{
    // Zeroed placeholders; each entry is filled later by PatchChunkEntry.
    const uint8_t zeros[CPKG_CHUNK_ENTRY_SIZE] = {};
    for (uint32_t i = 0; i < chunkCount; ++i)
        sink.WriteBytes(zeros, CPKG_CHUNK_ENTRY_SIZE);
}

void PatchChunkEntry(CCpkgSink& sink, uint64_t tableOffset, uint32_t entryIndex,
                     uint32_t fourcc, uint16_t version, uint64_t dataOffset, uint32_t sizeRaw)
{
    // Build the entry image and patch it in one seek. Version occupies the low 16 bits and Flags the
    // high 16 bits of the second word; Flags stays 0 in v1. SizeCompressed == SizeRaw (uncompressed).
    // The ChunkGUID region stays zeroed.
    uint8_t e[CPKG_CHUNK_ENTRY_SIZE] = {};
    std::memcpy(e + CPKG_CHUNK_OFFSET_FOURCC,          &fourcc,     sizeof(fourcc));
    std::memcpy(e + CPKG_CHUNK_OFFSET_VERSION,         &version,    sizeof(version));
    std::memcpy(e + CPKG_CHUNK_OFFSET_DATA_OFFSET,     &dataOffset, sizeof(dataOffset));
    std::memcpy(e + CPKG_CHUNK_OFFSET_SIZE_COMPRESSED, &sizeRaw,    sizeof(sizeRaw)); // SizeCompressed
    std::memcpy(e + CPKG_CHUNK_OFFSET_SIZE_RAW,        &sizeRaw,    sizeof(sizeRaw)); // SizeRaw

    const uint64_t base = tableOffset + static_cast<uint64_t>(entryIndex) * CPKG_CHUNK_ENTRY_SIZE;
    sink.PatchBytes(base, e, CPKG_CHUNK_ENTRY_SIZE); // failure latches the sink's sticky status
}

Gem::Result ReadCpkgHeader(CCpkgReader& reader, CpkgHeaderData* out, const PackageLogFn& logFn)
{
    if (!out)
    {
        LogF(logFn, PackageLogLevel::Error, "ReadCpkgHeader: null output pointer");
        return Gem::Result::BadPointer;
    }

    // Read the whole header into a local buffer so the CRC can be recomputed over the same bytes.
    uint8_t headerBytes[CPKG_HEADER_SIZE];
    if (!reader.ReadBytes(headerBytes, CPKG_HEADER_SIZE))
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadCpkgHeader: truncated file; need %zu header bytes, have %zu",
             CPKG_HEADER_SIZE, reader.BytesRemaining());
        return Gem::Result::CorruptedData; // premature end of data
    }

    CCpkgReader hr(headerBytes, CPKG_HEADER_SIZE);
    CpkgHeaderData h;
    h.Magic        = hr.ReadU32();
    h.VersionMajor = hr.ReadU16();
    h.VersionMinor = hr.ReadU16();
    h.ChunkCount   = hr.ReadU32();
    h.Flags        = hr.ReadU32();
    h.Reserved     = hr.ReadU32();
    h.HeaderCRC32  = hr.ReadU32();

    if (h.Magic != CPKG_MAGIC)
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadCpkgHeader: bad magic 0x%08X (expected 0x%08X for 'CPKG')",
             static_cast<unsigned>(h.Magic), static_cast<unsigned>(CPKG_MAGIC));
        return Gem::Result::InvalidArg;
    }

    if ((h.Flags & CPKG_FLAG_LITTLE_ENDIAN) == 0)
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadCpkgHeader: little-endian flag clear (Flags=0x%08X)", static_cast<unsigned>(h.Flags));
        return Gem::Result::InvalidArg;
    }

    const uint32_t computed = CRC32(headerBytes, CPKG_HEADER_CRC_COVERAGE);
    if (computed != h.HeaderCRC32)
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadCpkgHeader: header CRC mismatch (stored 0x%08X, computed 0x%08X)",
             static_cast<unsigned>(h.HeaderCRC32), static_cast<unsigned>(computed));
        return Gem::Result::CorruptedData; // integrity check failed
    }

    *out = h;
    return Gem::Result::Success;
}

Gem::Result ReadChunkTable(CCpkgReader& reader, uint32_t count, std::vector<ChunkEntryData>* out,
                           const PackageLogFn& logFn)
{
    if (!out)
    {
        LogF(logFn, PackageLogLevel::Error, "ReadChunkTable: null output pointer");
        return Gem::Result::BadPointer;
    }

    std::vector<ChunkEntryData> entries;
    entries.reserve(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        if (reader.BytesRemaining() < CPKG_CHUNK_ENTRY_SIZE)
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadChunkTable: truncated at entry %u of %u; need %zu bytes, have %zu",
                 i, count, CPKG_CHUNK_ENTRY_SIZE, reader.BytesRemaining());
            return Gem::Result::CorruptedData; // premature end of data
        }

        ChunkEntryData e;
        e.FourCC         = reader.ReadU32();
        e.Version        = reader.ReadU16();
        e.Flags          = reader.ReadU16();
        e.Offset         = reader.ReadU64();
        e.SizeCompressed = reader.ReadU32();
        e.SizeRaw        = reader.ReadU32();
        if (!reader.ReadBytes(e.ChunkGUID, sizeof(e.ChunkGUID)))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadChunkTable: truncated reading ChunkGUID of entry %u of %u", i, count);
            return Gem::Result::CorruptedData; // premature end of data
        }

        entries.push_back(e);
    }

    *out = std::move(entries);
    return Gem::Result::Success;
}

Gem::Result ReadCpkgHeader(CCpkgSource& source, CpkgHeaderData* out, const PackageLogFn& logFn)
{
    uint8_t headerBytes[CPKG_HEADER_SIZE];
    const Gem::Result readResult = source.Read(0, headerBytes, CPKG_HEADER_SIZE);
    if (Gem::Failed(readResult))
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadCpkgHeader: source read failed (need %zu header bytes, source size %llu)",
             CPKG_HEADER_SIZE, static_cast<unsigned long long>(source.Size()));
        // A range outside the source means the file is too small to hold its header -> corrupt.
        // Uninitialized (unopened source) and Fail (I/O error) propagate as-is.
        return (readResult == Gem::Result::InvalidArg) ? Gem::Result::CorruptedData : readResult;
    }

    CCpkgReader reader(headerBytes, CPKG_HEADER_SIZE);
    return ReadCpkgHeader(reader, out, logFn);
}

Gem::Result ReadChunkTable(CCpkgSource& source, uint32_t count, std::vector<ChunkEntryData>* out,
                           const PackageLogFn& logFn)
{
    const size_t tableBytes = CPKG_CHUNK_ENTRY_SIZE * static_cast<size_t>(count);
    std::vector<uint8_t> buf(tableBytes);
    if (tableBytes != 0)
    {
        const Gem::Result readResult = source.Read(CPKG_HEADER_SIZE, buf.data(), tableBytes);
        if (Gem::Failed(readResult))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadChunkTable: source read failed for %u entries (%zu bytes at offset %zu)",
                 count, tableBytes, CPKG_HEADER_SIZE);
            // Table runs past the end of the source -> corrupt. Uninitialized / Fail propagate.
            return (readResult == Gem::Result::InvalidArg) ? Gem::Result::CorruptedData : readResult;
        }
    }

    CCpkgReader reader(buf.data(), buf.size());
    return ReadChunkTable(reader, count, out, logFn);
}

} // namespace Canvas::Cpkg
