//================================================================================================
// ImageFiles - the bytes of small image files for tests of cpkg's image handling
//
// Each file holds the signature and header of its encoding, giving a width and height, and no
// pixel data a decoder could read: cpkg reads only headers and stores the bytes unchanged.
//================================================================================================

#pragma once

#include <cstdint>
#include <string>

namespace ImageFiles
{
    inline void AppendBigEndian(std::string *pBytes, uint32_t value, int size)
    {
        for (int shift = (size - 1) * 8; shift >= 0; shift -= 8)
            *pBytes += char((value >> shift) & 0xff);
    }

    inline void AppendLittleEndian(std::string *pBytes, uint32_t value, int size)
    {
        for (int shift = 0; shift < size * 8; shift += 8)
            *pBytes += char((value >> shift) & 0xff);
    }

    // A PNG file: its signature, an IHDR chunk, and an IEND chunk. tag varies the bytes of files
    // of one size.
    inline std::string Png(uint32_t width, uint32_t height, char tag = 0)
    {
        std::string bytes("\x89PNG\r\n\x1a\n", 8);
        AppendBigEndian(&bytes, 13, 4);
        bytes += "IHDR";
        AppendBigEndian(&bytes, width, 4);
        AppendBigEndian(&bytes, height, 4);
        bytes += std::string("\x08\x02\x00\x00\x00", 5);   // 8-bit RGB
        bytes += std::string("\x00\x00\x00", 3) + tag;     // CRC, unchecked
        AppendBigEndian(&bytes, 0, 4);
        bytes += "IEND";
        AppendBigEndian(&bytes, 0, 4);
        return bytes;
    }

    // A JPEG file: SOI, an APP0 segment and fill bytes before the SOF0 frame, and EOI.
    inline std::string Jpeg(uint32_t width, uint32_t height)
    {
        std::string bytes("\xff\xd8", 2);
        bytes += std::string("\xff\xe0", 2);
        AppendBigEndian(&bytes, 16, 2);
        bytes += std::string("JFIF\0\x01\x01\0\0\x01\0\x01\0\0", 14);
        bytes += std::string("\xff\xff\xc0", 3);
        AppendBigEndian(&bytes, 17, 2);
        bytes += char(8);
        AppendBigEndian(&bytes, height, 2);
        AppendBigEndian(&bytes, width, 2);
        bytes += char(3);
        bytes += std::string("\x01\x22\x00\x02\x11\x01\x03\x11\x01", 9);
        bytes += std::string("\xff\xd9", 2);
        return bytes;
    }

    // A TIFF file in either byte order: its header and one image file directory holding
    // ImageWidth as a LONG and ImageLength as a SHORT.
    inline std::string Tiff(uint32_t width, uint32_t height, bool bigEndian)
    {
        auto append = [bigEndian](std::string *pBytes, uint32_t value, int size)
        {
            if (bigEndian)
                AppendBigEndian(pBytes, value, size);
            else
                AppendLittleEndian(pBytes, value, size);
        };
        std::string bytes = bigEndian ? std::string("MM\x00\x2a", 4) : std::string("II\x2a\x00", 4);
        append(&bytes, 8, 4);
        append(&bytes, 2, 2);
        append(&bytes, 256, 2);
        append(&bytes, 4, 2);
        append(&bytes, 1, 4);
        append(&bytes, width, 4);
        append(&bytes, 257, 2);
        append(&bytes, 3, 2);
        append(&bytes, 1, 4);
        append(&bytes, height, 2);
        append(&bytes, 0, 2);
        append(&bytes, 0, 4);
        return bytes;
    }
}
