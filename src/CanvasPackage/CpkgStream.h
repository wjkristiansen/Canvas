#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

//--------------------------------------------------------------------------------------------------
// CpkgWriter - append-only binary stream backed by a byte vector.
// Tracks absolute file offset so callers can align to file boundaries.
//--------------------------------------------------------------------------------------------------
class CpkgWriter
{
public:
    void WriteBytes(const void* data, size_t count)
    {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        m_Buffer.insert(m_Buffer.end(), bytes, bytes + count);
    }

    void WriteU8(uint8_t v)   { WriteBytes(&v, 1); }
    void WriteU16(uint16_t v) { WriteBytes(&v, 2); }
    void WriteU32(uint32_t v) { WriteBytes(&v, 4); }
    void WriteU64(uint64_t v) { WriteBytes(&v, 8); }
    void WriteI32(int32_t v)  { WriteBytes(&v, 4); }

    void WriteFloat(float v)
    {
        // Use memcpy to avoid strict-aliasing violation.
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        WriteBytes(&bits, 4);
    }

    void WriteFloats(const float* data, size_t count) { WriteBytes(data, count * sizeof(float)); }
    void WriteU32s(const uint32_t* data, size_t count) { WriteBytes(data, count * sizeof(uint32_t)); }
    void WriteI32s(const int32_t* data, size_t count) { WriteBytes(data, count * sizeof(int32_t)); }

    // Append zero bytes until GetOffset() is a multiple of alignment.
    void PadToAlignment(size_t alignment)
    {
        if (alignment <= 1) return;
        size_t rem = m_Buffer.size() % alignment;
        if (rem != 0)
        {
            size_t pad = alignment - rem;
            m_Buffer.resize(m_Buffer.size() + pad, uint8_t(0));
        }
    }

    size_t GetOffset() const { return m_Buffer.size(); }

    void PatchU32(size_t offset, uint32_t value)
    {
        std::memcpy(m_Buffer.data() + offset, &value, 4);
    }

    void PatchU64(size_t offset, uint64_t value)
    {
        std::memcpy(m_Buffer.data() + offset, &value, 8);
    }

    const std::vector<uint8_t>& GetBuffer() const { return m_Buffer; }

private:
    std::vector<uint8_t> m_Buffer;
};

//--------------------------------------------------------------------------------------------------
// CpkgReader - cursor-based binary reader over a flat byte span.
// All multi-byte reads are from little-endian storage (native on x64 Windows).
// Returns false / throws on overrun depending on the method variant.
//--------------------------------------------------------------------------------------------------
class CpkgReader
{
public:
    CpkgReader(const uint8_t* data, size_t size)
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
        if (!ReadBytes(&v, 1)) throw std::out_of_range("CpkgReader overrun");
        return v;
    }

    uint16_t ReadU16()
    {
        uint16_t v = 0;
        if (!ReadBytes(&v, 2)) throw std::out_of_range("CpkgReader overrun");
        return v;
    }

    uint32_t ReadU32()
    {
        uint32_t v = 0;
        if (!ReadBytes(&v, 4)) throw std::out_of_range("CpkgReader overrun");
        return v;
    }

    uint64_t ReadU64()
    {
        uint64_t v = 0;
        if (!ReadBytes(&v, 8)) throw std::out_of_range("CpkgReader overrun");
        return v;
    }

    int32_t ReadI32()
    {
        int32_t v = 0;
        if (!ReadBytes(&v, 4)) throw std::out_of_range("CpkgReader overrun");
        return v;
    }

    float ReadFloat()
    {
        uint32_t bits = 0;
        if (!ReadBytes(&bits, 4)) throw std::out_of_range("CpkgReader overrun");
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
