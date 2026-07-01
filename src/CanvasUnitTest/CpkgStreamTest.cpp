#include "pch.h"
#include "CpkgStream.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace CanvasUnitTest
{

using namespace Canvas::Cpkg;

namespace
{
    // Append a scalar to a byte buffer in little-endian (host) order -- the storage order CCpkgReader
    // expects. Used to hand-build reader inputs now that the writer streams to a CCpkgSink (file).
    template <typename T>
    void Push(std::vector<uint8_t>& buf, T v)
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        buf.insert(buf.end(), p, p + sizeof(T));
    }
}

TEST(CpkgReaderTest, RoundTripScalars)
{
    std::vector<uint8_t> buf;
    Push<uint8_t>(buf, 0xABu);
    Push<uint16_t>(buf, 0x1234u);
    Push<uint32_t>(buf, 0xDEADBEEFu);
    Push<uint64_t>(buf, 0x0102030405060708ull);
    Push<int32_t>(buf, -42);
    Push<float>(buf, 3.14f);

    CCpkgReader r(buf.data(), buf.size());
    EXPECT_EQ(r.ReadU8(),  0xABu);
    EXPECT_EQ(r.ReadU16(), 0x1234u);
    EXPECT_EQ(r.ReadU32(), 0xDEADBEEFu);
    EXPECT_EQ(r.ReadU64(), 0x0102030405060708ull);
    EXPECT_EQ(r.ReadI32(), -42);
    EXPECT_FLOAT_EQ(r.ReadFloat(), 3.14f);
    EXPECT_TRUE(r.IsAtEnd());
}

TEST(CpkgReaderTest, FloatArray)
{
    float src[12];
    for (int i = 0; i < 12; ++i) src[i] = static_cast<float>(i) * 1.5f;

    std::vector<uint8_t> buf(sizeof(src));
    std::memcpy(buf.data(), src, sizeof(src));

    float dst[12] = {};
    CCpkgReader r(buf.data(), buf.size());
    EXPECT_TRUE(r.ReadFloats(dst, 12));
    for (int i = 0; i < 12; ++i)
        EXPECT_FLOAT_EQ(dst[i], src[i]);
}

TEST(CpkgReaderTest, OverrunReturnsFalse)
{
    std::vector<uint8_t> buf;
    Push<uint32_t>(buf, 0x1u);

    CCpkgReader r(buf.data(), buf.size());
    EXPECT_TRUE(r.ReadBytes(nullptr, 0)); // zero-byte read is fine
    (void)r.ReadU32();                    // consume the 4 bytes
    uint8_t dummy = 0;
    EXPECT_FALSE(r.ReadBytes(&dummy, 1)); // now at end -> false
}

TEST(CpkgReaderTest, OverrunThrows)
{
    std::vector<uint8_t> buf;
    Push<uint32_t>(buf, 0x1u);

    CCpkgReader r(buf.data(), buf.size());
    r.ReadU32(); // consume all 4 bytes
    EXPECT_THROW(r.ReadU8(), std::out_of_range);
}

TEST(CpkgReaderTest, SetOffset)
{
    std::vector<uint8_t> buf;
    Push<uint32_t>(buf, 0xAAAAAAAAu); // bytes 0-3
    Push<uint32_t>(buf, 0xBBBBBBBBu); // bytes 4-7

    CCpkgReader r(buf.data(), buf.size());
    r.SetOffset(4);
    EXPECT_EQ(r.ReadU32(), 0xBBBBBBBBu);
}

TEST(CpkgReaderTest, AlignToOffset)
{
    std::vector<uint8_t> buf(5);
    for (int i = 0; i < 5; ++i) buf[i] = uint8_t(i);

    CCpkgReader r(buf.data(), buf.size());
    for (int i = 0; i < 5; ++i) r.ReadU8(); // cursor at 5
    EXPECT_EQ(r.GetOffset(), size_t(5));
    r.AlignToOffset(4);
    EXPECT_EQ(r.GetOffset(), size_t(8));
}

} // namespace CanvasUnitTest
