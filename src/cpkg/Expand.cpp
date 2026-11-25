//================================================================================================
// Expand - the build's Expand stage
//
// Builds each import's content (see Import.cpp), then reads the header of each image a package
// source holds: the encoding its content begins with, and its width and height, decoding no
// pixels. See "Images" in src/cpkg/README.md.
//================================================================================================

#include "Stages.h"

namespace CpkgTool
{

using Canvas::CpkgImageEncoding;
namespace fs = std::filesystem;

namespace
{

//------------------------------------------------------------------------------------------------
// Encoded image headers

// Reads big- and little-endian integers from bytes, failing past their end.
class CReader
{
public:
    explicit CReader(std::string_view bytes) :
        m_Bytes(bytes)
    {
    }

    bool Has(size_t offset, size_t size) const { return offset <= m_Bytes.size() && size <= m_Bytes.size() - offset; }

    uint32_t Byte(size_t offset) const { return uint8_t(m_Bytes[offset]); }

    uint32_t U16(size_t offset, bool bigEndian) const
    {
        return bigEndian ? (Byte(offset) << 8) | Byte(offset + 1) : Byte(offset) | (Byte(offset + 1) << 8);
    }

    uint32_t U32(size_t offset, bool bigEndian) const
    {
        return bigEndian ? (U16(offset, true) << 16) | U16(offset + 2, true) : U16(offset, false) | (U16(offset + 2, false) << 16);
    }

    bool StartsWith(std::string_view prefix) const { return m_Bytes.substr(0, prefix.size()) == prefix; }

private:
    std::string_view m_Bytes;
};

constexpr std::string_view kPngSignature("\x89PNG\r\n\x1a\n", 8);
constexpr std::string_view kJpegSignature("\xff\xd8\xff", 3);
constexpr std::string_view kTiffLittle("II\x2a\x00", 4);
constexpr std::string_view kTiffBig("MM\x00\x2a", 4);

// A PNG file's size, from the IHDR chunk that follows its signature.
bool ReadPng(const CReader &reader, ImageHeader *pHeader)
{
    if (!reader.Has(8, 16) || reader.U32(8, true) != 13 || !reader.Has(12, 4))
        return false;
    if (reader.Byte(12) != 'I' || reader.Byte(13) != 'H' || reader.Byte(14) != 'D' || reader.Byte(15) != 'R')
        return false;
    pHeader->Width = reader.U32(16, true);
    pHeader->Height = reader.U32(20, true);
    return true;
}

// A JPEG file's size, from the first start-of-frame segment among those before its scan.
bool ReadJpeg(const CReader &reader, ImageHeader *pHeader)
{
    size_t offset = 2;
    for (;;)
    {
        // A marker is 0xFF, any fill bytes of 0xFF, and its code.
        if (!reader.Has(offset, 2) || reader.Byte(offset) != 0xff)
            return false;
        while (reader.Has(offset + 1, 1) && reader.Byte(offset + 1) == 0xff)
            ++offset;
        if (!reader.Has(offset + 1, 1))
            return false;
        const uint32_t code = reader.Byte(offset + 1);
        offset += 2;

        // Markers without a segment: TEM and the restart markers.
        if (code == 0x01 || (code >= 0xd0 && code <= 0xd7))
            continue;
        if (code == 0xd9 || code == 0xda)   // the end of the image, or its scan, before a frame
            return false;
        if (!reader.Has(offset, 2))
            return false;
        const uint32_t length = reader.U16(offset, true);
        if (length < 2 || !reader.Has(offset, length))
            return false;

        // SOF0-SOF15, other than DHT (C4), JPG (C8), and DAC (CC)
        const bool frame = code >= 0xc0 && code <= 0xcf && code != 0xc4 && code != 0xc8 && code != 0xcc;
        if (frame)
        {
            if (length < 7)
                return false;
            pHeader->Height = reader.U16(offset + 3, true);
            pHeader->Width = reader.U16(offset + 5, true);
            return true;
        }
        offset += length;
    }
}

// A TIFF file's size, from the ImageWidth and ImageLength tags of its first image file directory.
bool ReadTiff(const CReader &reader, bool bigEndian, ImageHeader *pHeader)
{
    if (!reader.Has(4, 4))
        return false;
    const size_t directory = reader.U32(4, bigEndian);
    if (!reader.Has(directory, 2))
        return false;
    const uint32_t entries = reader.U16(directory, bigEndian);
    if (!reader.Has(directory + 2, size_t(entries) * 12))
        return false;

    std::optional<uint32_t> width;
    std::optional<uint32_t> height;
    for (uint32_t i = 0; i < entries; ++i)
    {
        const size_t entry = directory + 2 + size_t(i) * 12;
        const uint32_t tag = reader.U16(entry, bigEndian);
        const uint32_t type = reader.U16(entry + 2, bigEndian);
        const uint32_t count = reader.U32(entry + 4, bigEndian);
        if ((tag != 256 && tag != 257) || count != 1)
            continue;

        // A SHORT value is left-justified in its 4-byte field; a LONG fills it.
        std::optional<uint32_t> value;
        if (type == 3)
            value = reader.U16(entry + 8, bigEndian);
        else if (type == 4)
            value = reader.U32(entry + 8, bigEndian);
        (tag == 256 ? width : height) = value;
    }
    if (!width || !height)
        return false;
    pHeader->Width = *width;
    pHeader->Height = *height;
    return true;
}

// The name of an encoding CanvasPkg does not store that bytes begin with the signature of, or null.
const char *OtherEncoding(const CReader &reader)
{
    if (reader.StartsWith("GIF87a") || reader.StartsWith("GIF89a"))
        return "GIF";
    if (reader.StartsWith("BM"))
        return "BMP";
    if (reader.StartsWith("RIFF") && reader.Has(8, 4) && reader.Byte(8) == 'W' && reader.Byte(9) == 'E' && reader.Byte(10) == 'B' && reader.Byte(11) == 'P')
        return "WebP";
    if (reader.StartsWith("DDS "))
        return "DDS";
    if (reader.StartsWith("\xabKTX 11\xbb") || reader.StartsWith("\xabKTX 20\xbb"))
        return "KTX";
    if (reader.StartsWith(std::string_view("\x76\x2f\x31\x01", 4)))
        return "OpenEXR";
    if (reader.StartsWith("#?RADIANCE") || reader.StartsWith("#?RGBE"))
        return "Radiance HDR";
    if (reader.StartsWith("8BPS"))
        return "Photoshop";
    if (reader.StartsWith(std::string_view("II\x2b\x00", 4)) || reader.StartsWith(std::string_view("MM\x00\x2b", 4)))
        return "BigTIFF";
    return nullptr;
}

// The encoding a file extension names, or Unknown for one that names none.
CpkgImageEncoding EncodingOfExtension(const fs::path &path)
{
    std::string extension = path.extension().string();
    for (char &c : extension)
    {
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    }
    if (extension == ".png")
        return CpkgImageEncoding::Png;
    if (extension == ".jpg" || extension == ".jpeg")
        return CpkgImageEncoding::Jpeg;
    if (extension == ".tif" || extension == ".tiff")
        return CpkgImageEncoding::Tiff;
    return CpkgImageEncoding::Unknown;
}

std::string Quoted(const std::string &text)
{
    return "\"" + text + "\"";
}

} // namespace

//------------------------------------------------------------------------------------------------
const char *ImageEncodingName(CpkgImageEncoding encoding)
{
    switch (encoding)
    {
    case CpkgImageEncoding::Png: return "PNG";
    case CpkgImageEncoding::Jpeg: return "JPEG";
    case CpkgImageEncoding::Tiff: return "TIFF";
    default: return nullptr;
    }
}

const char *ImageExtension(CpkgImageEncoding encoding)
{
    switch (encoding)
    {
    case CpkgImageEncoding::Png: return ".png";
    case CpkgImageEncoding::Jpeg: return ".jpg";
    case CpkgImageEncoding::Tiff: return ".tif";
    default: return nullptr;
    }
}

ImageRead ReadImageHeader(std::string_view bytes, ImageHeader *pHeader, const char **ppOtherEncoding)
{
    const CReader reader(bytes);
    *pHeader = {};
    *ppOtherEncoding = nullptr;

    bool read = false;
    if (reader.StartsWith(kPngSignature))
    {
        pHeader->Encoding = CpkgImageEncoding::Png;
        read = ReadPng(reader, pHeader);
    }
    else if (reader.StartsWith(kJpegSignature))
    {
        pHeader->Encoding = CpkgImageEncoding::Jpeg;
        read = ReadJpeg(reader, pHeader);
    }
    else if (reader.StartsWith(kTiffLittle) || reader.StartsWith(kTiffBig))
    {
        pHeader->Encoding = CpkgImageEncoding::Tiff;
        read = ReadTiff(reader, reader.StartsWith(kTiffBig), pHeader);
    }
    else
    {
        *ppOtherEncoding = OtherEncoding(reader);
        return ImageRead::Unstored;
    }

    if (!read || pHeader->Width == 0 || pHeader->Height == 0)
        return ImageRead::Damaged;
    return ImageRead::Read;
}

std::optional<uint32_t> Texture3DDepth(uint64_t count, uint32_t mipLevels)
{
    // The entries grow with the depth, so a binary search over depths finds the one, if any.
    auto entries = [mipLevels](uint64_t depth)
    {
        uint64_t total = 0;
        for (uint32_t m = 0; m < mipLevels && m < 64; ++m)
            total += std::max<uint64_t>(1, depth >> m);
        return total + (mipLevels > 64 ? mipLevels - 64 : 0);
    };
    uint64_t low = 1;
    uint64_t high = std::min<uint64_t>(count, UINT32_MAX);
    while (low <= high)
    {
        const uint64_t depth = low + (high - low) / 2;
        const uint64_t total = entries(depth);
        if (total == count)
            return uint32_t(depth);
        if (total < count)
            low = depth + 1;
        else
            high = depth - 1;
    }
    return std::nullopt;
}

//------------------------------------------------------------------------------------------------
bool ExpandPackageSource(SourcePackage &package, CDiagnostics &diagnostics)
{
    const size_t errorCount = diagnostics.GetErrorCount();
    ExpandImports(package, diagnostics);
    for (SourceBundle &bundle : package.Bundles)
    {
        for (SourceNamed<SourceImage> &asset : bundle.Images)
        {
            SourceImage &image = asset.Value;
            image.Headers.assign(image.Files.size(), ImageHeader{});
            for (size_t i = 0; i < image.Files.size(); ++i)
            {
                const SourceFileRef &file = image.Files[i];
                auto report = [&](Severity severity, const std::string &message)
                {
                    ReportAt(diagnostics, package, severity, file.Where, message + ": " + Quoted(file.Written));
                };

                std::string read;
                std::string_view bytes;
                fs::path path;
                if (image.Embedded)
                    bytes = *image.Embedded;
                else
                {
                    RecordInput(package, file.Path);
                    path = PathOf(file.Path);
                    const FileResult result = ReadWholeFile(path, &read);
                    if (result != FileResult::Success)
                    {
                        report(Severity::Error, result == FileResult::NotFound ? "file not found" : "cannot read the file");
                        continue;
                    }
                    bytes = read;
                }

                ImageHeader header;
                const char *pOther = nullptr;
                switch (ReadImageHeader(bytes, &header, &pOther))
                {
                case ImageRead::Unstored:
                    report(Severity::Error, pOther
                        ? std::string("expected a PNG, JPEG, or TIFF image, not a ") + pOther + " image"
                        : std::string("expected a PNG, JPEG, or TIFF image"));
                    continue;
                case ImageRead::Damaged:
                    report(Severity::Error, std::string("the ") + ImageEncodingName(header.Encoding) + " header gives no width and height");
                    continue;
                case ImageRead::Read:
                    break;
                }

                const CpkgImageEncoding named = EncodingOfExtension(image.Embedded ? PathOf(file.Written) : path);
                if (named != CpkgImageEncoding::Unknown && named != header.Encoding)
                {
                    report(Severity::Warning, std::string("the extension names a ") + ImageEncodingName(named) + " image, but the file holds a "
                        + ImageEncodingName(header.Encoding) + " image");
                }
                image.Headers[i] = header;
            }
        }
    }
    return diagnostics.GetErrorCount() == errorCount;
}

} // namespace CpkgTool
