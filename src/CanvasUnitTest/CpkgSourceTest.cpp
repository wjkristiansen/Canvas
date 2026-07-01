#include "pch.h"
#include "CpkgIO.h"
#include "CpkgSink.h"
#include "CpkgSource.h"
#include "CpkgTestUtil.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace CanvasUnitTest
{

using namespace Canvas::Cpkg;

TEST(CpkgSourceTest, FileSourceReadsRangeAndRejectsOverrun)
{
    TempFile tmp(L"canvas_cpkg_source_range.bin");
    {
        CCpkgSink sink;
        ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
        const uint8_t data[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
        sink.WriteBytes(data, sizeof(data));
        ASSERT_EQ(sink.Close(), Gem::Result::Success);
    }

    CCpkgSource src;
    ASSERT_EQ(CCpkgSource::OpenFile(tmp.wstr().c_str(), &src), Gem::Result::Success);
    EXPECT_EQ(src.Size(), 8u);

    uint8_t out[4] = {};
    ASSERT_EQ(src.Read(2, out, 4), Gem::Result::Success);
    EXPECT_EQ(out[0], 2u);
    EXPECT_EQ(out[3], 5u);

    EXPECT_EQ(src.Read(6, out, 4), Gem::Result::InvalidArg); // 6 + 4 > 8
    EXPECT_EQ(src.Read(8, out, 1), Gem::Result::InvalidArg); // starts at end
    EXPECT_EQ(src.Read(8, out, 0), Gem::Result::Success);    // zero-size at end is fine
}

TEST(CpkgSourceTest, EmptySourceReadFails)
{
    CCpkgSource src; // default: empty backing
    EXPECT_EQ(src.Size(), 0u);
    uint8_t out[1] = {};
    EXPECT_EQ(src.Read(0, out, 1), Gem::Result::Uninitialized);
}

TEST(CpkgSourceTest, FileSourceHeaderAndTableRoundTrip)
{
    TempFile tmp(L"canvas_cpkg_source_table.cpkg");
    {
        CCpkgSink sink;
        ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
        WriteCpkgHeader(sink, 2);
        uint64_t tableOffset = sink.Tell();
        WriteChunkTable(sink, 2);
        PatchChunkEntry(sink, tableOffset, 0, CPKG_FOURCC_NODE, 1, 100u, 8u);
        PatchChunkEntry(sink, tableOffset, 1, CPKG_FOURCC_MESH, 1, 200u, 16u);
        ASSERT_EQ(sink.Close(), Gem::Result::Success);
    }

    CCpkgSource src;
    ASSERT_EQ(CCpkgSource::OpenFile(tmp.wstr().c_str(), &src), Gem::Result::Success);

    CpkgHeaderData h;
    ASSERT_EQ(ReadCpkgHeader(src, &h), Gem::Result::Success);
    EXPECT_EQ(h.ChunkCount, 2u);

    std::vector<ChunkEntryData> entries;
    ASSERT_EQ(ReadChunkTable(src, h.ChunkCount, &entries), Gem::Result::Success);
    ASSERT_EQ(entries.size(), size_t(2));
    EXPECT_EQ(entries[1].FourCC, CPKG_FOURCC_MESH);
    EXPECT_EQ(entries[1].Offset, 200u);
    EXPECT_EQ(entries[1].SizeRaw, 16u);
}

// A file too small to hold a header is a corrupt/truncated package, not just a bad range: the
// container translates the source's out-of-range InvalidArg into CorruptedData.
TEST(CpkgSourceTest, UndersizedSourceHeaderReadIsCorrupt)
{
    TempFile tmp(L"canvas_cpkg_source_tiny.cpkg");
    {
        CCpkgSink sink;
        ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
        const uint8_t tiny[CPKG_HEADER_SIZE - 1] = {}; // one byte short of a full header
        sink.WriteBytes(tiny, sizeof(tiny));
        ASSERT_EQ(sink.Close(), Gem::Result::Success);
    }

    CCpkgSource src;
    ASSERT_EQ(CCpkgSource::OpenFile(tmp.wstr().c_str(), &src), Gem::Result::Success);

    CpkgHeaderData h;
    EXPECT_EQ(ReadCpkgHeader(src, &h), Gem::Result::CorruptedData);
}

// End-to-end streaming model: stream a container to disk, then read its header and chunk table and
// pull only one chunk's payload range -- never the whole file.
TEST(CpkgSourceTest, FileSourceStreamsChunkByOffset)
{
    TempFile tmp(L"canvas_cpkg_source_stream.cpkg");
    uint64_t dataOffset = 0;
    uint32_t size       = 0;
    {
        CCpkgSink sink;
        ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
        WriteCpkgHeader(sink, 1);
        uint64_t tableOffset = sink.Tell();
        WriteChunkTable(sink, 1);
        sink.PadToAlignment(4);
        dataOffset = sink.Tell();
        sink.WriteU32(0x12345678u);
        size = static_cast<uint32_t>(sink.Tell() - dataOffset);
        PatchChunkEntry(sink, tableOffset, 0, CPKG_FOURCC_MESH, 1, dataOffset, size);
        ASSERT_EQ(sink.Close(), Gem::Result::Success);
    }

    CCpkgSource src;
    ASSERT_EQ(CCpkgSource::OpenFile(tmp.wstr().c_str(), &src), Gem::Result::Success);
    EXPECT_GT(src.Size(), 0u);

    CpkgHeaderData h;
    ASSERT_EQ(ReadCpkgHeader(src, &h), Gem::Result::Success);
    EXPECT_EQ(h.ChunkCount, 1u);

    std::vector<ChunkEntryData> entries;
    ASSERT_EQ(ReadChunkTable(src, h.ChunkCount, &entries), Gem::Result::Success);
    ASSERT_EQ(entries.size(), size_t(1));
    EXPECT_EQ(entries[0].FourCC, CPKG_FOURCC_MESH);

    // Stream just the chunk payload by its recorded offset/size.
    uint8_t payload[4] = {};
    ASSERT_EQ(src.Read(entries[0].Offset, payload, entries[0].SizeRaw), Gem::Result::Success);
    CCpkgReader pr(payload, sizeof(payload));
    EXPECT_EQ(pr.ReadU32(), 0x12345678u);
}

TEST(CpkgSourceTest, FileSourceOpenMissingFileFails)
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "canvas_cpkg_does_not_exist.cpkg";
    std::error_code ec;
    std::filesystem::remove(path, ec); // ensure absent

    CCpkgSource src;
    EXPECT_TRUE(Gem::Failed(CCpkgSource::OpenFile(path.wstring().c_str(), &src)));
    EXPECT_EQ(src.Size(), 0u); // unchanged on failure
}

} // namespace CanvasUnitTest
