#include "pch.h"
#include "CpkgIO.h"
#include "CpkgSink.h"
#include "CpkgSource.h"
#include "CpkgTestUtil.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace CanvasUnitTest
{

using namespace Canvas::Cpkg;

namespace
{
    // Hand-build a header block with a chosen chunk count and flags, then stamp the correct CRC over
    // the covered prefix. Lets the reader-rejection tests craft controlled malformed inputs without
    // depending on the streaming writer.
    std::vector<uint8_t> MakeHeaderBytes(uint32_t chunkCount, uint32_t flags)
    {
        std::vector<uint8_t> b(CPKG_HEADER_SIZE, 0u);
        const uint32_t magic    = CPKG_MAGIC;
        const uint16_t vMajor   = CPKG_VERSION_MAJOR;
        const uint16_t vMinor   = CPKG_VERSION_MINOR;
        const uint32_t reserved = 0u;
        std::memcpy(b.data() + CPKG_HEADER_OFFSET_MAGIC,         &magic,      sizeof(magic));
        std::memcpy(b.data() + CPKG_HEADER_OFFSET_VERSION_MAJOR, &vMajor,     sizeof(vMajor));
        std::memcpy(b.data() + CPKG_HEADER_OFFSET_VERSION_MINOR, &vMinor,     sizeof(vMinor));
        std::memcpy(b.data() + CPKG_HEADER_OFFSET_CHUNK_COUNT,   &chunkCount, sizeof(chunkCount));
        std::memcpy(b.data() + CPKG_HEADER_OFFSET_FLAGS,         &flags,      sizeof(flags));
        std::memcpy(b.data() + CPKG_HEADER_OFFSET_RESERVED,      &reserved,   sizeof(reserved));
        const uint32_t crc = CRC32(b.data(), CPKG_HEADER_CRC_COVERAGE);
        std::memcpy(b.data() + CPKG_HEADER_OFFSET_CRC, &crc, sizeof(crc));
        return b;
    }
}

TEST(CpkgIOTest, Crc32StandardVector)
{
    const char* s = "123456789";
    uint32_t crc = CRC32(reinterpret_cast<const uint8_t*>(s), 9);
    EXPECT_EQ(crc, 0xCBF43926u);
}

// The streamed magic must serialize to ASCII 'C','P','K','G' at offset 0, never byte-reversed.
TEST(CpkgIOTest, MagicBytesAreAsciiCpkg)
{
    TempFile tmp(L"canvas_cpkg_io_magic.cpkg");

    CCpkgSink sink;
    ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
    WriteCpkgHeader(sink, 0);
    ASSERT_EQ(sink.Close(), Gem::Result::Success);

    std::vector<uint8_t> buf = ReadFileBytes(tmp.path);
    ASSERT_GE(buf.size(), size_t(4));
    EXPECT_EQ(buf[0], uint8_t('C'));
    EXPECT_EQ(buf[1], uint8_t('P'));
    EXPECT_EQ(buf[2], uint8_t('K'));
    EXPECT_EQ(buf[3], uint8_t('G'));
}

TEST(CpkgIOTest, HeaderAndChunkTableRoundTrip)
{
    TempFile tmp(L"canvas_cpkg_io_table.cpkg");

    {
        CCpkgSink sink;
        ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
        WriteCpkgHeader(sink, 2);
        uint64_t tableOffset = sink.Tell();
        WriteChunkTable(sink, 2);
        PatchChunkEntry(sink, tableOffset, 0, CPKG_FOURCC_NODE, 1, 1000u, 64u);
        PatchChunkEntry(sink, tableOffset, 1, CPKG_FOURCC_MESH, 2, 2000u, 128u);
        ASSERT_EQ(sink.Close(), Gem::Result::Success);
    }

    CCpkgSource src;
    ASSERT_EQ(CCpkgSource::OpenFile(tmp.wstr().c_str(), &src), Gem::Result::Success);

    CpkgHeaderData h;
    ASSERT_EQ(ReadCpkgHeader(src, &h), Gem::Result::Success);
    EXPECT_EQ(h.Magic, CPKG_MAGIC);
    EXPECT_EQ(h.VersionMajor, CPKG_VERSION_MAJOR);
    EXPECT_EQ(h.VersionMinor, CPKG_VERSION_MINOR);
    EXPECT_EQ(h.ChunkCount, 2u);
    EXPECT_NE(h.Flags & CPKG_FLAG_LITTLE_ENDIAN, 0u);

    std::vector<ChunkEntryData> entries;
    ASSERT_EQ(ReadChunkTable(src, h.ChunkCount, &entries), Gem::Result::Success);
    ASSERT_EQ(entries.size(), size_t(2));

    EXPECT_EQ(entries[0].FourCC, CPKG_FOURCC_NODE);
    EXPECT_EQ(entries[0].Version, 1u);
    EXPECT_EQ(entries[0].Flags, 0u);
    EXPECT_EQ(entries[0].Offset, 1000u);
    EXPECT_EQ(entries[0].SizeCompressed, 64u);
    EXPECT_EQ(entries[0].SizeRaw, 64u);

    EXPECT_EQ(entries[1].FourCC, CPKG_FOURCC_MESH);
    EXPECT_EQ(entries[1].Version, 2u);
    EXPECT_EQ(entries[1].Offset, 2000u);
    EXPECT_EQ(entries[1].SizeRaw, 128u);
}

TEST(CpkgIOTest, ReadRejectsBadMagic)
{
    std::vector<uint8_t> buf = MakeHeaderBytes(0, CPKG_FLAG_LITTLE_ENDIAN);
    buf[CPKG_HEADER_OFFSET_MAGIC] = uint8_t('X'); // corrupt the magic (checked before the CRC)

    CCpkgReader r(buf.data(), buf.size());
    CpkgHeaderData h;
    EXPECT_EQ(ReadCpkgHeader(r, &h), Gem::Result::InvalidArg); // malformed package data
}

TEST(CpkgIOTest, ReadRejectsBadCrc)
{
    std::vector<uint8_t> buf = MakeHeaderBytes(0, CPKG_FLAG_LITTLE_ENDIAN);
    buf[CPKG_HEADER_OFFSET_CRC] ^= 0xFFu; // corrupt the stored CRC field (magic and flags stay valid)

    CCpkgReader r(buf.data(), buf.size());
    CpkgHeaderData h;

    // The failure must carry enough detail to diagnose it. The sink composes the record itself
    // (deferred formatting), mirroring how a real QLog sink works.
    std::string logged;
    PackageLogLevel level = PackageLogLevel::Info;
    auto logFn = [&](PackageLogLevel lvl, const char* fmt, va_list args)
    {
        level = lvl;
        char line[256];
        std::vsnprintf(line, sizeof(line), fmt, args);
        logged = line;
    };

    EXPECT_EQ(ReadCpkgHeader(r, &h, logFn), Gem::Result::CorruptedData); // integrity failure
    EXPECT_EQ(level, PackageLogLevel::Error);
    EXPECT_NE(logged.find("CRC mismatch"), std::string::npos);
}

TEST(CpkgIOTest, ReadRejectsNonLittleEndianFlag)
{
    std::vector<uint8_t> buf = MakeHeaderBytes(0, 0u); // flag clear, but CRC valid for these bytes

    CCpkgReader r(buf.data(), buf.size());
    CpkgHeaderData h;
    EXPECT_EQ(ReadCpkgHeader(r, &h), Gem::Result::InvalidArg);
}

TEST(CpkgIOTest, ReadRejectsNullOutPointer)
{
    std::vector<uint8_t> buf = MakeHeaderBytes(0, CPKG_FLAG_LITTLE_ENDIAN);
    CCpkgReader r(buf.data(), buf.size());
    EXPECT_EQ(ReadCpkgHeader(r, nullptr), Gem::Result::BadPointer);
}

TEST(CpkgIOTest, ReadRejectsTruncatedHeaderAsCorrupt)
{
    // One byte short of a full header -> premature end of data.
    uint8_t buf[CPKG_HEADER_SIZE - 1] = {};
    CCpkgReader r(buf, sizeof(buf));
    CpkgHeaderData h;
    EXPECT_EQ(ReadCpkgHeader(r, &h), Gem::Result::CorruptedData);
}

// Exercise the full streaming write pattern with a single 4-byte dummy chunk, then read it back
// from the file by its recorded offset/size.
TEST(CpkgIOTest, StreamingRoundTrip)
{
    TempFile tmp(L"canvas_cpkg_io_roundtrip.cpkg");

    const uint32_t chunkCount = 1;
    uint64_t dataOffset = 0;
    uint32_t size       = 0;

    {
        CCpkgSink sink;
        ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
        WriteCpkgHeader(sink, chunkCount);
        uint64_t tableOffset = sink.Tell();
        WriteChunkTable(sink, chunkCount);

        sink.PadToAlignment(4);
        dataOffset = sink.Tell();
        sink.WriteU32(0xCAFEBABEu);
        size = static_cast<uint32_t>(sink.Tell() - dataOffset);

        PatchChunkEntry(sink, tableOffset, 0, CPKG_FOURCC_NODE, 1, dataOffset, size);
        ASSERT_EQ(sink.Close(), Gem::Result::Success);
    }

    CCpkgSource src;
    ASSERT_EQ(CCpkgSource::OpenFile(tmp.wstr().c_str(), &src), Gem::Result::Success);

    CpkgHeaderData h;
    ASSERT_EQ(ReadCpkgHeader(src, &h), Gem::Result::Success);
    EXPECT_EQ(h.ChunkCount, chunkCount);

    std::vector<ChunkEntryData> entries;
    ASSERT_EQ(ReadChunkTable(src, h.ChunkCount, &entries), Gem::Result::Success);
    ASSERT_EQ(entries.size(), size_t(1));
    EXPECT_EQ(entries[0].FourCC, CPKG_FOURCC_NODE);
    EXPECT_EQ(entries[0].Offset, dataOffset);
    EXPECT_EQ(entries[0].SizeRaw, size);
    EXPECT_EQ(size, 4u);

    // The recorded offset must land back on the dummy chunk payload.
    uint8_t payload[4] = {};
    ASSERT_EQ(src.Read(entries[0].Offset, payload, entries[0].SizeRaw), Gem::Result::Success);
    CCpkgReader pr(payload, sizeof(payload));
    EXPECT_EQ(pr.ReadU32(), 0xCAFEBABEu);
}

} // namespace CanvasUnitTest
