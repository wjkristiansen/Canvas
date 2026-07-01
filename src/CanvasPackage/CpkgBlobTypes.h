//================================================================================================
// CpkgBlobTypes - constants and plain-data structs describing the .cpkg container layout.
//
// These mirror the on-disk header and chunk-table binary format exactly (see README.md).
// Internal to CanvasPackage; not part of the public Inc/ surface.
//================================================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace Canvas::Cpkg
{

//--------------------------------------------------------------------------------------------------
// MakeFourCC - packs four ASCII characters into a uint32 with 'a' in the lowest byte, so that
// writing the result little-endian places the characters in file order.
//--------------------------------------------------------------------------------------------------
constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
    return  static_cast<uint32_t>(static_cast<uint8_t>(a))
         | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
         | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

// File magic. Stored little-endian, this serializes to the ASCII bytes 'C','P','K','G' at offset 0.
constexpr uint32_t CPKG_MAGIC = MakeFourCC('C', 'P', 'K', 'G');

constexpr uint16_t CPKG_VERSION_MAJOR = 1;
constexpr uint16_t CPKG_VERSION_MINOR = 0;

//--------------------------------------------------------------------------------------------------
// On-disk header field offsets and total size, each derived from the preceding field's width so a
// field-list change recomputes every downstream offset and the size. The serializer uses these
// constants instead of raw byte numbers, and the static_assert below ties them to CpkgHeaderData.
//--------------------------------------------------------------------------------------------------
constexpr size_t CPKG_HEADER_OFFSET_MAGIC         = 0;
constexpr size_t CPKG_HEADER_OFFSET_VERSION_MAJOR = CPKG_HEADER_OFFSET_MAGIC         + sizeof(uint32_t);
constexpr size_t CPKG_HEADER_OFFSET_VERSION_MINOR = CPKG_HEADER_OFFSET_VERSION_MAJOR + sizeof(uint16_t);
constexpr size_t CPKG_HEADER_OFFSET_CHUNK_COUNT   = CPKG_HEADER_OFFSET_VERSION_MINOR + sizeof(uint16_t);
constexpr size_t CPKG_HEADER_OFFSET_FLAGS         = CPKG_HEADER_OFFSET_CHUNK_COUNT   + sizeof(uint32_t);
constexpr size_t CPKG_HEADER_OFFSET_RESERVED      = CPKG_HEADER_OFFSET_FLAGS         + sizeof(uint32_t);
constexpr size_t CPKG_HEADER_OFFSET_CRC           = CPKG_HEADER_OFFSET_RESERVED      + sizeof(uint32_t);
constexpr size_t CPKG_HEADER_SIZE                 = CPKG_HEADER_OFFSET_CRC           + sizeof(uint32_t);
// The header CRC covers every byte preceding the CRC field, i.e. exactly the CRC field's offset.
constexpr size_t CPKG_HEADER_CRC_COVERAGE         = CPKG_HEADER_OFFSET_CRC;

//--------------------------------------------------------------------------------------------------
// On-disk chunk-table entry field offsets and total size, derived the same way (tied to
// ChunkEntryData below).
//--------------------------------------------------------------------------------------------------
constexpr size_t CPKG_CHUNK_GUID_SIZE = 16;
constexpr size_t CPKG_CHUNK_OFFSET_FOURCC          = 0;
constexpr size_t CPKG_CHUNK_OFFSET_VERSION         = CPKG_CHUNK_OFFSET_FOURCC          + sizeof(uint32_t);
constexpr size_t CPKG_CHUNK_OFFSET_FLAGS           = CPKG_CHUNK_OFFSET_VERSION         + sizeof(uint16_t);
constexpr size_t CPKG_CHUNK_OFFSET_DATA_OFFSET     = CPKG_CHUNK_OFFSET_FLAGS           + sizeof(uint16_t);
constexpr size_t CPKG_CHUNK_OFFSET_SIZE_COMPRESSED = CPKG_CHUNK_OFFSET_DATA_OFFSET     + sizeof(uint64_t);
constexpr size_t CPKG_CHUNK_OFFSET_SIZE_RAW        = CPKG_CHUNK_OFFSET_SIZE_COMPRESSED + sizeof(uint32_t);
constexpr size_t CPKG_CHUNK_OFFSET_GUID            = CPKG_CHUNK_OFFSET_SIZE_RAW        + sizeof(uint32_t);
constexpr size_t CPKG_CHUNK_ENTRY_SIZE             = CPKG_CHUNK_OFFSET_GUID            + CPKG_CHUNK_GUID_SIZE;

// Header Flags bit 0: asserts the file is little-endian. Loaders reject the file if it is clear.
constexpr uint32_t CPKG_FLAG_LITTLE_ENDIAN = 0x1u;

// Chunk type identifiers. The seven v1 chunk types plus the reserved dependency list (DEPS),
// whose FourCC is defined now so a v2 loader is not constrained.
constexpr uint32_t CPKG_FOURCC_NODE = MakeFourCC('N', 'O', 'D', 'E');
constexpr uint32_t CPKG_FOURCC_MESH = MakeFourCC('M', 'E', 'S', 'H');
constexpr uint32_t CPKG_FOURCC_MATL = MakeFourCC('M', 'A', 'T', 'L');
constexpr uint32_t CPKG_FOURCC_TXTR = MakeFourCC('T', 'X', 'T', 'R');
constexpr uint32_t CPKG_FOURCC_LITE = MakeFourCC('L', 'I', 'T', 'E');
constexpr uint32_t CPKG_FOURCC_CAMR = MakeFourCC('C', 'A', 'M', 'R');
constexpr uint32_t CPKG_FOURCC_ANIM = MakeFourCC('A', 'N', 'I', 'M');
constexpr uint32_t CPKG_FOURCC_DEPS = MakeFourCC('D', 'E', 'P', 'S'); // reserved for v2+

//--------------------------------------------------------------------------------------------------
// CpkgHeaderData - in-memory mirror of the file header. Field order and widths match the on-disk
// layout exactly (see the CPKG_HEADER_OFFSET_* constants and the static_assert below).
//--------------------------------------------------------------------------------------------------
struct CpkgHeaderData
{
    uint32_t Magic        = 0; // CPKG_MAGIC
    uint16_t VersionMajor = 0;
    uint16_t VersionMinor = 0;
    uint32_t ChunkCount   = 0; // number of entries in the chunk table
    uint32_t Flags        = 0; // bit 0 = little-endian (CPKG_FLAG_LITTLE_ENDIAN)
    uint32_t Reserved     = 0; // 0 in v1
    uint32_t HeaderCRC32  = 0; // CRC32 of the header bytes preceding this field
};

//--------------------------------------------------------------------------------------------------
// ChunkEntryData - in-memory mirror of one chunk-table entry. Field order and widths match the
// on-disk layout exactly (see the CPKG_CHUNK_OFFSET_* constants and the static_assert below).
//--------------------------------------------------------------------------------------------------
struct ChunkEntryData
{
    uint32_t FourCC         = 0;
    uint16_t Version        = 0;  // chunk format version, independent of the header version
    uint16_t Flags          = 0;  // bit 0 = compressed (reserved, always 0 in v1)
    uint64_t Offset         = 0;  // byte offset from file start to the chunk data
    uint32_t SizeCompressed = 0;  // bytes on disk (== SizeRaw when uncompressed)
    uint32_t SizeRaw        = 0;  // uncompressed byte count
    uint8_t  ChunkGUID[CPKG_CHUNK_GUID_SIZE] = {}; // zeroed in v1; reserved for dedup / hot-reload / cross-package refs
};

// Tripwires: the mirror structs must stay tightly packed and identical in size to the derived
// on-disk layout. If a field is added or padding creeps in, these fail at compile time rather than
// letting the struct silently diverge from the bytes the serializer reads and writes.
static_assert(sizeof(CpkgHeaderData) == CPKG_HEADER_SIZE,      "CpkgHeaderData drifted from CPKG_HEADER_SIZE");
static_assert(sizeof(ChunkEntryData) == CPKG_CHUNK_ENTRY_SIZE, "ChunkEntryData drifted from CPKG_CHUNK_ENTRY_SIZE");

} // namespace Canvas::Cpkg
