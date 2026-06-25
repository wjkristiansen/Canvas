#include "pch.h"
#include "CpkgStream.h"

namespace CanvasUnitTest
{

TEST(CpkgStreamTest, RoundTripScalars)
{
    CpkgWriter w;
    w.WriteU8(0xABu);
    w.WriteU16(0x1234u);
    w.WriteU32(0xDEADBEEFu);
    w.WriteU64(0x0102030405060708ull);
    w.WriteI32(-42);
    w.WriteFloat(3.14f);

    const auto& buf = w.GetBuffer();
    CpkgReader r(buf.data(), buf.size());

    EXPECT_EQ(r.ReadU8(),  0xABu);
    EXPECT_EQ(r.ReadU16(), 0x1234u);
    EXPECT_EQ(r.ReadU32(), 0xDEADBEEFu);
    EXPECT_EQ(r.ReadU64(), 0x0102030405060708ull);
    EXPECT_EQ(r.ReadI32(), -42);
    EXPECT_FLOAT_EQ(r.ReadFloat(), 3.14f);
    EXPECT_TRUE(r.IsAtEnd());
}

TEST(CpkgStreamTest, FloatArray)
{
    float src[12];
    for (int i = 0; i < 12; ++i) src[i] = static_cast<float>(i) * 1.5f;

    CpkgWriter w;
    w.WriteFloats(src, 12);

    float dst[12] = {};
    const auto& buf = w.GetBuffer();
    CpkgReader r(buf.data(), buf.size());
    EXPECT_TRUE(r.ReadFloats(dst, 12));
    for (int i = 0; i < 12; ++i)
        EXPECT_FLOAT_EQ(dst[i], src[i]);
}

TEST(CpkgStreamTest, PadTo4)
{
    CpkgWriter w;
    w.WriteU8(0xFF);
    w.PadToAlignment(4);
    EXPECT_EQ(w.GetOffset(), size_t(4));
}

TEST(CpkgStreamTest, PadTo16)
{
    CpkgWriter w;
    // Write 13 bytes.
    for (int i = 0; i < 13; ++i) w.WriteU8(uint8_t(i));
    w.PadToAlignment(16);
    EXPECT_EQ(w.GetOffset(), size_t(16));
}

TEST(CpkgStreamTest, PatchU32)
{
    CpkgWriter w;
    size_t patchOffset = w.GetOffset();
    w.WriteU32(0u);         // placeholder
    w.WriteU32(0x11223344u); // some other data after the patch site

    w.PatchU32(patchOffset, 0xDEADBEEFu);

    const auto& buf = w.GetBuffer();
    CpkgReader r(buf.data(), buf.size());
    EXPECT_EQ(r.ReadU32(), 0xDEADBEEFu);
    EXPECT_EQ(r.ReadU32(), 0x11223344u);
}

TEST(CpkgStreamTest, OverrunReturnsFalse)
{
    CpkgWriter w;
    w.WriteU32(0x1u);

    const auto& buf = w.GetBuffer();
    CpkgReader r(buf.data(), buf.size());
    // Consume the 4 bytes, then attempt to read one more.
    EXPECT_TRUE(r.ReadBytes(nullptr, 0)); // zero-byte read is fine
    uint32_t v = r.ReadU32();
    (void)v;
    // Now at end -- next read must return false.
    uint8_t dummy = 0;
    EXPECT_FALSE(r.ReadBytes(&dummy, 1));
}

TEST(CpkgStreamTest, OverrunThrows)
{
    CpkgWriter w;
    w.WriteU32(0x1u);

    const auto& buf = w.GetBuffer();
    CpkgReader r(buf.data(), buf.size());
    r.ReadU32(); // consume all 4 bytes
    EXPECT_THROW(r.ReadU8(), std::out_of_range);
}

TEST(CpkgStreamTest, SetOffset)
{
    CpkgWriter w;
    w.WriteU32(0xAAAAAAAAu); // bytes 0-3
    w.WriteU32(0xBBBBBBBBu); // bytes 4-7

    const auto& buf = w.GetBuffer();
    CpkgReader r(buf.data(), buf.size());
    r.SetOffset(4);
    EXPECT_EQ(r.ReadU32(), 0xBBBBBBBBu);
}

TEST(CpkgStreamTest, AlignToOffset)
{
    CpkgWriter w;
    for (int i = 0; i < 5; ++i) w.WriteU8(uint8_t(i));

    const auto& buf = w.GetBuffer();
    CpkgReader r(buf.data(), buf.size());
    // Consume 5 bytes; cursor is at 5.
    for (int i = 0; i < 5; ++i) r.ReadU8();
    EXPECT_EQ(r.GetOffset(), size_t(5));
    r.AlignToOffset(4);
    EXPECT_EQ(r.GetOffset(), size_t(8));
}

} // namespace CanvasUnitTest
