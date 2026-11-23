//================================================================================================
// CpkgImageTest - Tests for cpkg's images: reading their files' headers, validating their
// shapes, writing CpkgImageData, and unpacking it
//================================================================================================

#include "pch.h"
#include "CanvasPkg.h"
#include "CpkgTool.h"
#include "ImageFiles.h"
#include "SchemaCheck.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    void WriteBytes(const std::string &path, std::string_view bytes)
    {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path().empty() ? "." : std::filesystem::path(path).parent_path());
        std::ofstream file(std::filesystem::path(path), std::ios::binary);
        file.write(bytes.data(), std::streamsize(bytes.size()));
    }

    std::string ReadBytes(const std::string &path)
    {
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    struct RunResult
    {
        int Status = -1;
        std::string Err;
    };

    RunResult RunCpkg(std::vector<std::string> args)
    {
        args.insert(args.begin(), "cpkg");
        std::vector<const char *> argv;
        for (const std::string &arg : args)
            argv.push_back(arg.c_str());
        std::ostringstream out;
        std::ostringstream err;
        RunResult result;
        result.Status = CpkgTool::Run(int(argv.size()), argv.data(), out, err);
        result.Err = out.str() + err.str();
        return result;
    }

    std::vector<std::string> FilesUnder(const std::string &folder)
    {
        std::vector<std::string> files;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(folder))
        {
            if (entry.is_regular_file())
                files.push_back(entry.path().lexically_relative(folder).generic_string());
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    // One asset's payload, in a buffer aligned for the payload helpers.
    class CPayload
    {
    public:
        CPayload(const CCpkgReader &reader, uint32_t bundleIndex, uint32_t assetIndex)
        {
            m_Size = size_t(reader.GetAssetCompression(bundleIndex, assetIndex).RawSize);
            m_Blocks.resize((m_Size + sizeof(Block) - 1) / sizeof(Block));
            EXPECT_EQ(reader.ReadAsset(bundleIndex, assetIndex, std::as_writable_bytes(std::span(m_Blocks))), Result::Success);
        }

        const CpkgImageData &Header() const
        {
            const CpkgImageData *pHeader = nullptr;
            EXPECT_EQ(CpkgGetPayloadHeader(Bytes(), &pHeader), Result::Success);
            return *pHeader;
        }

        // The bytes of each of the image's encoded images.
        std::vector<std::string> Images() const
        {
            std::span<const CpkgSpan<uint8_t>> entries;
            EXPECT_EQ((CpkgResolveSpan<CpkgImageData>(Bytes(), Header().Images, &entries)), Result::Success);
            std::vector<std::string> images;
            for (const CpkgSpan<uint8_t> &entry : entries)
            {
                std::span<const uint8_t> bytes;
                EXPECT_EQ((CpkgResolveSpan<CpkgImageData>(Bytes(), entry, &bytes)), Result::Success);
                images.emplace_back(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            }
            return images;
        }

    private:
        std::span<const std::byte> Bytes() const { return std::as_bytes(std::span(m_Blocks)).first(m_Size); }

        struct alignas(kCpkgPayloadAlignment) Block
        {
            std::byte Bytes[kCpkgPayloadAlignment];
        };
        std::vector<Block> m_Blocks;
        size_t m_Size = 0;
    };

    // A package of each image form, its files written beside it.
    const char *const kSource = R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Looks",
      "images": {
        "Albedo": "art/albedo.png",
        "Sky": { "cubemap": { "+X": "sky/px.jpg", "-X": "sky/nx.jpg", "+Y": "sky/py.jpg", "-Y": "sky/ny.jpg", "+Z": "sky/pz.jpg", "-Z": "sky/nz.jpg" } },
        "Mips": { "dimension": "texture2D", "arraySize": 2, "mipLevels": 3,
                  "files": ["mips/a0.tif", "mips/a1.tif", "mips/a2.tif", "mips/b0.tif", "mips/b1.tif", "mips/b2.tif"] },
        "Line": { "dimension": "texture1D", "mipLevels": 2, "files": ["line0.png", "line1.png"] },
        "Volume": { "dimension": "texture3D", "mipLevels": 2, "files": ["v0.png", "v1.png", "v2.png", "v3.png"] }
      },
      "materials": { "Brass": { "textures": { "albedo": "Albedo" } } },
      "skyboxes": { "Day": { "image": "Sky" } }
    }
  ]
}
)";

    void WriteSourceFiles()
    {
        WriteBytes("levels.cpkg.json", kSource);
        WriteBytes("art/albedo.png", ImageFiles::Png(4, 2));
        for (const char *name : { "px", "nx", "py", "ny", "pz", "nz" })
            WriteBytes(std::string("sky/") + name + ".jpg", ImageFiles::Jpeg(8, 8));
        WriteBytes("mips/a0.tif", ImageFiles::Tiff(4, 4, false));
        WriteBytes("mips/a1.tif", ImageFiles::Tiff(2, 2, false));
        WriteBytes("mips/a2.tif", ImageFiles::Tiff(1, 1, false));
        WriteBytes("mips/b0.tif", ImageFiles::Tiff(4, 4, true));
        WriteBytes("mips/b1.tif", ImageFiles::Tiff(2, 2, true));
        WriteBytes("mips/b2.tif", ImageFiles::Tiff(1, 1, true));
        WriteBytes("line0.png", ImageFiles::Png(8, 1));
        WriteBytes("line1.png", ImageFiles::Png(4, 1));
        WriteBytes("v0.png", ImageFiles::Png(4, 4, 'a'));
        WriteBytes("v1.png", ImageFiles::Png(4, 4, 'b'));
        WriteBytes("v2.png", ImageFiles::Png(4, 4, 'c'));
        WriteBytes("v3.png", ImageFiles::Png(2, 2));
    }

    std::string Package(const std::string &bundle)
    {
        return R"({ "formatVersion": 1, "packageName": "Levels", "bundles": [ )" + bundle + " ] }\n";
    }
}

//------------------------------------------------------------------------------------------------
// Runs each test in its own empty current directory, so paths in diagnostics are relative to it.
class CpkgImage : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo *pInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        m_Dir = std::filesystem::temp_directory_path() / (std::string("CpkgImage_") + pInfo->name());
        std::filesystem::remove_all(m_Dir);
        std::filesystem::create_directories(m_Dir);
        m_PreviousDir = std::filesystem::current_path();
        std::filesystem::current_path(m_Dir);
    }

    void TearDown() override
    {
        std::filesystem::current_path(m_PreviousDir);
        std::error_code ignored;
        std::filesystem::remove_all(m_Dir, ignored);
    }

private:
    std::filesystem::path m_Dir;
    std::filesystem::path m_PreviousDir;
};

TEST_F(CpkgImage, BuildsEachForm)
{
    WriteSourceFiles();
    const RunResult built = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(built.Err, "");
    ASSERT_EQ(built.Status, CpkgTool::kExitSuccess);

    CCpkgReader reader;
    ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
    ASSERT_EQ(reader.GetAssetCount(0), 7u);

    struct Expected
    {
        const char *Name;
        CpkgImageEncoding Encoding;
        CpkgImageDimension Dimension;
        uint32_t Width, Height, Depth, ArraySize, MipLevels;
        std::vector<std::string> Files;
    };
    const Expected expected[] = {
        { "Albedo", CpkgImageEncoding::Png, CpkgImageDimension::Texture2D, 4, 2, 1, 1, 1, { "art/albedo.png" } },
        { "Sky", CpkgImageEncoding::Jpeg, CpkgImageDimension::Cube, 8, 8, 1, 6, 1,
          { "sky/px.jpg", "sky/nx.jpg", "sky/py.jpg", "sky/ny.jpg", "sky/pz.jpg", "sky/nz.jpg" } },
        { "Mips", CpkgImageEncoding::Tiff, CpkgImageDimension::Texture2D, 4, 4, 1, 2, 3,
          { "mips/a0.tif", "mips/a1.tif", "mips/a2.tif", "mips/b0.tif", "mips/b1.tif", "mips/b2.tif" } },
        { "Line", CpkgImageEncoding::Png, CpkgImageDimension::Texture1D, 8, 1, 1, 1, 2, { "line0.png", "line1.png" } },
        { "Volume", CpkgImageEncoding::Png, CpkgImageDimension::Texture3D, 4, 4, 3, 1, 2, { "v0.png", "v1.png", "v2.png", "v3.png" } },
    };
    for (uint32_t i = 0; i < std::size(expected); ++i)
    {
        const Expected &image = expected[i];
        EXPECT_EQ(reader.GetAssetType(0, i), AssetType::Image) << image.Name;
        EXPECT_EQ(reader.GetAssetName(0, i), image.Name);
        const CPayload payload(reader, 0, i);
        const CpkgImageData &header = payload.Header();
        EXPECT_EQ(header.Encoding, image.Encoding) << image.Name;
        EXPECT_EQ(header.Dimension, image.Dimension) << image.Name;
        EXPECT_EQ(header.Width, image.Width) << image.Name;
        EXPECT_EQ(header.Height, image.Height) << image.Name;
        EXPECT_EQ(header.Depth, image.Depth) << image.Name;
        EXPECT_EQ(header.ArraySize, image.ArraySize) << image.Name;
        EXPECT_EQ(header.MipLevels, image.MipLevels) << image.Name;

        // Each file's bytes are stored unchanged, in the source's order.
        const std::vector<std::string> images = payload.Images();
        ASSERT_EQ(images.size(), image.Files.size()) << image.Name;
        for (size_t f = 0; f < images.size(); ++f)
            EXPECT_EQ(images[f], ReadBytes(image.Files[f])) << image.Name << " " << f;
    }
    EXPECT_EQ(reader.GetAssetType(0, 5), AssetType::Skybox);
}

TEST_F(CpkgImage, ReportsFilesItCannotStore)
{
    WriteBytes("levels.cpkg.json", Package(R"({ "name": "Looks", "images": {
    "Missing": "missing.png", "Gif": "anim.gif", "Text": "notes.txt", "Damaged": "broken.png" } })"));
    WriteBytes("anim.gif", "GIF89a....");
    WriteBytes("notes.txt", "not an image");
    WriteBytes("broken.png", std::string("\x89PNG\r\n\x1a\n", 8));

    const RunResult result = RunCpkg({ "validate", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:2:16: error: file not found: \"missing.png\" [/bundles/0/images/Missing]\n"
        "levels.cpkg.json:2:38: error: expected a PNG, JPEG, or TIFF image, not a GIF image: \"anim.gif\" [/bundles/0/images/Gif]\n"
        "levels.cpkg.json:2:58: error: expected a PNG, JPEG, or TIFF image: \"notes.txt\" [/bundles/0/images/Text]\n"
        "levels.cpkg.json:2:82: error: the PNG header gives no width and height: \"broken.png\" [/bundles/0/images/Damaged]\n");
}

TEST_F(CpkgImage, WarnsOfAnExtensionThatNamesAnotherEncoding)
{
    WriteBytes("levels.cpkg.json", Package(R"({ "name": "Looks", "images": { "Photo": "photo.png" } })"));
    WriteBytes("photo.png", ImageFiles::Jpeg(2, 2));

    // The file's content decides its encoding.
    const RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:1:101: warning: the extension names a PNG image, but the file holds a JPEG image: \"photo.png\" [/bundles/0/images/Photo]\n");
    ASSERT_EQ(result.Status, CpkgTool::kExitSuccess);
    CCpkgReader reader;
    ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
    EXPECT_EQ(CPayload(reader, 0, 0).Header().Encoding, CpkgImageEncoding::Jpeg);
    reader.Close();

    EXPECT_EQ(RunCpkg({ "build", "levels.cpkg.json", "--werror" }).Status, CpkgTool::kExitError);
}

TEST_F(CpkgImage, ValidatesShapes)
{
    WriteBytes("levels.cpkg.json", Package(R"({ "name": "Looks", "images": {
    "Short": { "dimension": "texture2D", "mipLevels": 2, "files": ["a4.png"] },
    "Cube": { "dimension": "cube", "arraySize": 2, "files": ["a4.png", "a4.png"] },
    "NoDepth": { "dimension": "texture3D", "mipLevels": 2, "files": ["a4.png", "a4.png", "a4.png", "a4.png", "a2.png"] },
    "Layered": { "dimension": "texture3D", "arraySize": 2, "files": ["a4.png", "a4.png"] },
    "Mixed": { "dimension": "texture2D", "arraySize": 2, "files": ["a4.png", "a4.jpg"] },
    "Mips": { "dimension": "texture2D", "mipLevels": 2, "files": ["a4.png", "a1.png"] },
    "Tall": { "dimension": "texture1D", "files": ["a4.png"] },
    "Oblong": { "cubemap": { "+X": "w4.png", "-X": "w4.png", "+Y": "w4.png", "-Y": "w4.png", "+Z": "w4.png", "-Z": "w4.png" } },
    "Deep": { "dimension": "texture2D", "mipLevels": 4, "files": ["a4.png", "a2.png", "a1.png", "a1.png"] }
  } })"));
    WriteBytes("a4.png", ImageFiles::Png(4, 4));
    WriteBytes("a2.png", ImageFiles::Png(2, 2));
    WriteBytes("a1.png", ImageFiles::Png(1, 1));
    WriteBytes("a4.jpg", ImageFiles::Jpeg(4, 4));
    WriteBytes("w4.png", ImageFiles::Png(4, 2));

    const RunResult result = RunCpkg({ "validate", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:2:14: error: expected 2 files, for 1 array slice of 2 mip levels; found 1 [/bundles/0/images/Short]\n"
        "levels.cpkg.json:3:13: error: expected an arraySize that is a multiple of 6 for a cube image; found 2 [/bundles/0/images/Cube]\n"
        "levels.cpkg.json:4:16: error: 5 files fit no depth of a 3D image of 2 mip levels, whose mip m holds max(1, depth >> m) files [/bundles/0/images/NoDepth]\n"
        "levels.cpkg.json:5:16: error: expected an arraySize of 1 for a 3D image; found 2 [/bundles/0/images/Layered]\n"
        "levels.cpkg.json:6:78: error: expected a PNG image, the encoding of the image's first file; the file is a JPEG image: \"a4.jpg\" [/bundles/0/images/Mixed/files/1]\n"
        "levels.cpkg.json:7:77: error: expected 2x2, the size of mip 1; the file is 1x1: \"a1.png\" [/bundles/0/images/Mips/files/1]\n"
        "levels.cpkg.json:8:51: error: expected a file 1 pixel high for a 1D image; the file is 4x4: \"a4.png\" [/bundles/0/images/Tall/files/0]\n"
        "levels.cpkg.json:9:36: error: expected a square file for a cube image; the file is 4x2: \"w4.png\" [/bundles/0/images/Oblong/cubemap/+X]\n"
        "levels.cpkg.json:10:13: error: expected at most 3 mip levels, the full mip chain of a 4x4 image; found 4 [/bundles/0/images/Deep]\n");
}

TEST_F(CpkgImage, UnpacksEachForm)
{
    WriteSourceFiles();
    ASSERT_EQ(RunCpkg({ "build", "levels.cpkg.json" }).Status, CpkgTool::kExitSuccess);

    const RunResult unpacked = RunCpkg({ "unpack", "levels.cpkg", "-o", "out/levels.cpkg.json" });
    EXPECT_EQ(unpacked.Err, "");
    ASSERT_EQ(unpacked.Status, CpkgTool::kExitSuccess);
    EXPECT_TRUE(MatchesSchema(CanvasSchema::PackageSource, "out/Looks/Looks.cbundle.json"));
    EXPECT_EQ(FilesUnder("out"), (std::vector<std::string>{
        "Looks/Albedo.png", "Looks/Line.0.png", "Looks/Line.1.png", "Looks/Looks.cbundle.json",
        "Looks/Mips.0.tif", "Looks/Mips.1.tif", "Looks/Mips.2.tif", "Looks/Mips.3.tif", "Looks/Mips.4.tif", "Looks/Mips.5.tif",
        "Looks/Sky.+X.jpg", "Looks/Sky.+Y.jpg", "Looks/Sky.+Z.jpg", "Looks/Sky.-X.jpg", "Looks/Sky.-Y.jpg", "Looks/Sky.-Z.jpg",
        "Looks/Volume.0.png", "Looks/Volume.1.png", "Looks/Volume.2.png", "Looks/Volume.3.png", "levels.cpkg.json" }));

    // Each image in the simplest form that holds it: a file, a cubemap, or the general form.
    EXPECT_EQ(ReadBytes("out/Looks/Looks.cbundle.json"), R"({
  "formatVersion": 1,
  "bundle": {
    "name": "Looks",
    "images": {
      "Albedo": "Albedo.png",
      "Sky": {
        "cubemap": {
          "+X": "Sky.+X.jpg",
          "-X": "Sky.-X.jpg",
          "+Y": "Sky.+Y.jpg",
          "-Y": "Sky.-Y.jpg",
          "+Z": "Sky.+Z.jpg",
          "-Z": "Sky.-Z.jpg"
        }
      },
      "Mips": {
        "dimension": "texture2D",
        "arraySize": 2,
        "mipLevels": 3,
        "files": [
          "Mips.0.tif", "Mips.1.tif", "Mips.2.tif", "Mips.3.tif", "Mips.4.tif", "Mips.5.tif"
        ]
      },
      "Line": { "dimension": "texture1D", "mipLevels": 2, "files": ["Line.0.png", "Line.1.png"] },
      "Volume": {
        "dimension": "texture3D",
        "mipLevels": 2,
        "files": ["Volume.0.png", "Volume.1.png", "Volume.2.png", "Volume.3.png"]
      }
    },
    "materials": { "Brass": { "textures": { "albedo": "Albedo" } } },
    "skyboxes": { "Day": { "image": "Sky" } }
  }
}
)");

    // The unpacked files are the source's, byte for byte.
    EXPECT_EQ(ReadBytes("out/Looks/Albedo.png"), ReadBytes("art/albedo.png"));
    EXPECT_EQ(ReadBytes("out/Looks/Sky.-Z.jpg"), ReadBytes("sky/nz.jpg"));
    EXPECT_EQ(ReadBytes("out/Looks/Mips.3.tif"), ReadBytes("mips/b0.tif"));

    ASSERT_EQ(RunCpkg({ "build", "out/levels.cpkg.json", "-o", "rebuilt.cpkg" }).Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(ReadBytes("rebuilt.cpkg"), ReadBytes("levels.cpkg"));
}
