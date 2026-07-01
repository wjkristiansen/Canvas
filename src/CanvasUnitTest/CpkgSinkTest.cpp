#include "pch.h"
#include "CpkgSink.h"
#include "CpkgStream.h"
#include "CpkgTestUtil.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace CanvasUnitTest
{

using namespace Canvas::Cpkg;

TEST(CpkgSinkTest, WriteScalarsRoundTrip)
{
    TempFile tmp(L"canvas_cpkg_sink_scalars.bin");

    {
        CCpkgSink sink;
        ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
        sink.WriteU8(0xABu);
        sink.WriteU16(0x1234u);
        sink.WriteU32(0xDEADBEEFu);
        sink.WriteU64(0x0102030405060708ull);
        sink.WriteI32(-42);
        sink.WriteFloat(3.14f);
        EXPECT_EQ(sink.Tell(), 1u + 2u + 4u + 8u + 4u + 4u);
        ASSERT_EQ(sink.Close(), Gem::Result::Success);
    }

    std::vector<uint8_t> buf = ReadFileBytes(tmp.path);
    CCpkgReader r(buf.data(), buf.size());
    EXPECT_EQ(r.ReadU8(),  0xABu);
    EXPECT_EQ(r.ReadU16(), 0x1234u);
    EXPECT_EQ(r.ReadU32(), 0xDEADBEEFu);
    EXPECT_EQ(r.ReadU64(), 0x0102030405060708ull);
    EXPECT_EQ(r.ReadI32(), -42);
    EXPECT_FLOAT_EQ(r.ReadFloat(), 3.14f);
    EXPECT_TRUE(r.IsAtEnd());
}

TEST(CpkgSinkTest, PadToAlignment)
{
    TempFile tmp(L"canvas_cpkg_sink_pad.bin");

    CCpkgSink sink;
    ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
    sink.WriteU8(0xFFu);
    sink.PadToAlignment(4);
    EXPECT_EQ(sink.Tell(), 4u);
    ASSERT_EQ(sink.Close(), Gem::Result::Success);

    std::vector<uint8_t> buf = ReadFileBytes(tmp.path);
    ASSERT_EQ(buf.size(), size_t(4));
    EXPECT_EQ(buf[0], 0xFFu);
    EXPECT_EQ(buf[1], 0u);
    EXPECT_EQ(buf[2], 0u);
    EXPECT_EQ(buf[3], 0u);
}

TEST(CpkgSinkTest, PatchBytesOverwritesEarlierBytes)
{
    TempFile tmp(L"canvas_cpkg_sink_patch.bin");

    CCpkgSink sink;
    ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
    sink.WriteU32(0u);          // placeholder at offset 0
    sink.WriteU32(0x11223344u); // data after the patch site
    const uint32_t patched = 0xDEADBEEFu;
    ASSERT_EQ(sink.PatchBytes(0, &patched, sizeof patched), Gem::Result::Success);
    EXPECT_EQ(sink.Tell(), 8u); // patch does not move the append position
    ASSERT_EQ(sink.Close(), Gem::Result::Success);

    std::vector<uint8_t> buf = ReadFileBytes(tmp.path);
    CCpkgReader r(buf.data(), buf.size());
    EXPECT_EQ(r.ReadU32(), 0xDEADBEEFu);
    EXPECT_EQ(r.ReadU32(), 0x11223344u);
}

// A patch beyond what has been written is a programming error: PatchBytes reports InvalidArg and
// latches it so the whole write fails fast at Close().
TEST(CpkgSinkTest, PatchBytesPastEndIsRejected)
{
    TempFile tmp(L"canvas_cpkg_sink_patch_oob.bin");

    CCpkgSink sink;
    ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
    sink.WriteU32(0u); // only 4 bytes written
    uint32_t v = 0u;
    EXPECT_EQ(sink.PatchBytes(4, &v, sizeof v), Gem::Result::InvalidArg); // starts at end
    EXPECT_EQ(sink.Status(), Gem::Result::InvalidArg);                 // latched
    EXPECT_EQ(sink.Close(), Gem::Result::InvalidArg);
}

// A write at least as large as the flush cache bypasses it and streams straight to disk; the bytes
// must still land correctly and contiguously after smaller cached writes.
TEST(CpkgSinkTest, LargeWriteBypassesCache)
{
    TempFile tmp(L"canvas_cpkg_sink_large.bin");

    const size_t cache = 256;
    std::vector<uint8_t> big(cache * 4);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<uint8_t>(i * 7 + 1);

    CCpkgSink sink;
    ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink, cache), Gem::Result::Success);
    sink.WriteU32(0xCAFEBABEu);          // small cached write first
    sink.WriteBytes(big.data(), big.size()); // bypasses the cache
    EXPECT_EQ(sink.Tell(), 4u + big.size());
    ASSERT_EQ(sink.Close(), Gem::Result::Success);

    std::vector<uint8_t> buf = ReadFileBytes(tmp.path);
    ASSERT_EQ(buf.size(), 4u + big.size());
    CCpkgReader r(buf.data(), buf.size());
    EXPECT_EQ(r.ReadU32(), 0xCAFEBABEu);
    for (size_t i = 0; i < big.size(); ++i)
        EXPECT_EQ(buf[4 + i], big[i]);
}

TEST(CpkgSinkTest, CreateFileBadPathFails)
{
    // A path into a directory that does not exist cannot be opened for writing.
    std::filesystem::path bad =
        std::filesystem::temp_directory_path() / L"canvas_cpkg_no_such_dir" / L"out.cpkg";

    CCpkgSink sink;
    EXPECT_TRUE(Gem::Failed(CCpkgSink::CreateFile(bad.wstring().c_str(), &sink)));
    EXPECT_EQ(sink.Tell(), 0u);
}

TEST(CpkgSinkTest, WriteToUnopenedSinkLatchesUninitialized)
{
    CCpkgSink sink; // never opened
    sink.WriteU32(0u);
    EXPECT_EQ(sink.Status(), Gem::Result::Uninitialized);
    EXPECT_EQ(sink.Tell(), 0u);
}

} // namespace CanvasUnitTest
