#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace Canvas::Cpkg
{

//--------------------------------------------------------------------------------------------------
// CCpkgReader - cursor-based binary reader over a flat byte span (e.g. a header / chunk-table block
// or a single chunk payload that the caller has already pulled from a CCpkgSource).
// All multi-byte reads are from little-endian storage.
// Returns false / throws on overrun depending on the method variant.
//--------------------------------------------------------------------------------------------------
class CCpkgReader
{
public:
    CCpkgReader(const uint8_t* data, size_t size)
        : m_Data(data), m_Size(size), m_Offset(0)
    {}

    bool ReadBytes(void* out, size_t count)
    {
        if (m_Offset + count > m_Size) return false;
        std::memcpy(out, m_Data + m_Offset, count);
        m_Offset += count;
        return true;
    }

    uint8_t ReadU8()
    {
        uint8_t v = 0;
        if (!ReadBytes(&v, 1)) throw std::out_of_range("CCpkgReader overrun");
        return v;
    }

    uint16_t ReadU16()
    {
        uint16_t v = 0;
        if (!ReadBytes(&v, 2)) throw std::out_of_range("CCpkgReader overrun");
        return v;
    }

    uint32_t ReadU32()
    {
        uint32_t v = 0;
        if (!ReadBytes(&v, 4)) throw std::out_of_range("CCpkgReader overrun");
        return v;
    }

    uint64_t ReadU64()
    {
        uint64_t v = 0;
        if (!ReadBytes(&v, 8)) throw std::out_of_range("CCpkgReader overrun");
        return v;
    }

    int32_t ReadI32()
    {
        int32_t v = 0;
        if (!ReadBytes(&v, 4)) throw std::out_of_range("CCpkgReader overrun");
        return v;
    }

    float ReadFloat()
    {
        uint32_t bits = 0;
        if (!ReadBytes(&bits, 4)) throw std::out_of_range("CCpkgReader overrun");
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }

    bool ReadFloats(float* out, size_t count) { return ReadBytes(out, count * sizeof(float)); }
    bool ReadU32s(uint32_t* out, size_t count) { return ReadBytes(out, count * sizeof(uint32_t)); }
    bool ReadI32s(int32_t* out, size_t count) { return ReadBytes(out, count * sizeof(int32_t)); }

    bool SkipBytes(size_t n)
    {
        if (m_Offset + n > m_Size) return false;
        m_Offset += n;
        return true;
    }

    // Advance cursor to the next multiple of alignment.
    void AlignToOffset(size_t alignment)
    {
        if (alignment <= 1) return;
        size_t rem = m_Offset % alignment;
        if (rem != 0) m_Offset += alignment - rem;
    }

    size_t GetOffset() const { return m_Offset; }

    void SetOffset(size_t offset) { m_Offset = offset; }

    size_t BytesRemaining() const { return (m_Offset <= m_Size) ? (m_Size - m_Offset) : 0; }

    bool IsAtEnd() const { return m_Offset >= m_Size; }

private:
    const uint8_t* m_Data;
    size_t         m_Size;
    size_t         m_Offset;
};

} // namespace Canvas::Cpkg
