//================================================================================================
// CanvasPkgFileTest - Tests for CCpkgWriter, CCpkgReader, and CCpkgNameDirectory
//================================================================================================

#include "pch.h"
#include "CanvasPkg.h"

#include <filesystem>
#include <fstream>
#include <functional>

namespace
{
    std::string ToUtf8(const std::filesystem::path &path)
    {
        const std::u8string utf8 = path.u8string();
        return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
    }

    std::vector<std::byte> ReadBytes(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        std::vector<char> chars((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::vector<std::byte> bytes(chars.size());
        std::memcpy(bytes.data(), chars.data(), chars.size());
        return bytes;
    }

    void WriteBytes(const std::filesystem::path &path, std::span<const std::byte> bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    }

    template <class T>
    T ReadAt(const std::vector<std::byte> &bytes, uint64_t offset)
    {
        T value;
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }

    template <class T>
    void PatchAt(std::vector<std::byte> &bytes, uint64_t offset, const std::function<void(T &)> &patch)
    {
        T value = ReadAt<T>(bytes, offset);
        patch(value);
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
    }

    std::span<const std::byte> AsBytes(std::string_view text)
    {
        return { reinterpret_cast<const std::byte *>(text.data()), text.size() };
    }

    // A payload buffer aligned for the payload helpers.
    struct alignas(kCpkgPayloadAlignment) PayloadBlock
    {
        std::byte Bytes[kCpkgPayloadAlignment];
    };

    std::span<std::byte> AsSpan(std::vector<PayloadBlock> &blocks, size_t size)
    {
        return { reinterpret_cast<std::byte *>(blocks.data()), size };
    }
}

//------------------------------------------------------------------------------------------------
class CanvasPkgFile : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo *pInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        m_Dir = std::filesystem::temp_directory_path() / (std::string("CanvasPkgFile_") + pInfo->name());
        std::filesystem::remove_all(m_Dir);
        std::filesystem::create_directories(m_Dir);
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_Dir, ignored);
    }

    std::filesystem::path PathOf(const char *name) const { return m_Dir / name; }
    std::string FileName(const char *name) const { return ToUtf8(PathOf(name)); }

    // Writes "Sample": bundle "Materials" with two shared assets, an empty bundle "Empty", and
    // bundle "Goblin" with named and unnamed local assets.
    void WriteSample(const char *name)
    {
        CCpkgWriter writer;
        ASSERT_EQ(writer.Open(FileName(name).c_str(), "Sample"), Result::Success);

        uint32_t bundleIndex = UINT32_MAX;
        ASSERT_EQ(writer.BeginBundle("Materials", &bundleIndex), Result::Success);
        EXPECT_EQ(bundleIndex, 0u);

        CCpkgPayloadBuilder<CpkgMaterialData> material;
        CpkgMaterialData brass;
        const std::string_view albedo = "BrassAlbedo";
        ASSERT_EQ(material.Append(std::span(albedo.data(), albedo.size()), &brass.AlbedoTexture), Result::Success);
        ASSERT_EQ(material.SetHeader(brass), Result::Success);

        uint32_t assetIndex = UINT32_MAX;
        ASSERT_EQ(writer.AppendAsset(AssetType::Material, "Brass", CpkgCodec::None, material.GetPayload(), &assetIndex), Result::Success);
        EXPECT_EQ(assetIndex, 0u);
        ASSERT_EQ(writer.AppendAsset(AssetType::Image, "BrassAlbedo", CpkgCodec::None, AsBytes("not really a png"), &assetIndex), Result::Success);
        EXPECT_EQ(assetIndex, 1u);
        ASSERT_EQ(writer.EndBundle(), Result::Success);

        ASSERT_EQ(writer.BeginBundle("Empty", &bundleIndex), Result::Success);
        EXPECT_EQ(bundleIndex, 1u);
        ASSERT_EQ(writer.EndBundle(), Result::Success);

        ASSERT_EQ(writer.BeginBundle("Goblin"), Result::Success);
        CpkgTransformNodeData root;
        CpkgTransformNodeData hand;
        hand.ParentIndex = 0;
        CpkgCameraData camera;
        camera.NodeIndex = 1;
        ASSERT_EQ(writer.AppendAsset(AssetType::TransformNode, "", CpkgCodec::None, std::as_bytes(std::span(&root, 1))), Result::Success);
        ASSERT_EQ(writer.AppendAsset(AssetType::TransformNode, "Hand_R", CpkgCodec::None, std::as_bytes(std::span(&hand, 1))), Result::Success);
        ASSERT_EQ(writer.AppendAsset(AssetType::Camera, "", CpkgCodec::None, std::as_bytes(std::span(&camera, 1)), &assetIndex), Result::Success);
        EXPECT_EQ(assetIndex, 2u);
        ASSERT_EQ(writer.EndBundle(), Result::Success);

        ASSERT_EQ(writer.Close(), Result::Success);
    }

    Result OpenBytes(std::span<const std::byte> bytes)
    {
        const std::filesystem::path path = PathOf("patched.cpkg");
        WriteBytes(path, bytes);
        CCpkgReader reader;
        return reader.Open(ToUtf8(path).c_str());
    }

    std::filesystem::path m_Dir;
};

//------------------------------------------------------------------------------------------------
TEST_F(CanvasPkgFile, RoundTrip)
{
    WriteSample("sample.cpkg");

    CCpkgReader reader;
    ASSERT_EQ(reader.Open(FileName("sample.cpkg").c_str()), Result::Success);
    EXPECT_EQ(reader.GetPackageName(), "Sample");
    ASSERT_EQ(reader.GetBundleCount(), 3u);
    EXPECT_EQ(reader.GetBundleName(0), "Materials");
    EXPECT_EQ(reader.GetBundleName(1), "Empty");
    EXPECT_EQ(reader.GetBundleName(2), "Goblin");
    EXPECT_EQ(reader.GetAssetCount(0), 2u);
    EXPECT_EQ(reader.GetAssetCount(1), 0u);
    ASSERT_EQ(reader.GetAssetCount(2), 3u);

    EXPECT_EQ(reader.GetAssetType(0, 0), AssetType::Material);
    EXPECT_EQ(reader.GetAssetName(0, 0), "Brass");
    EXPECT_EQ(reader.GetAssetType(0, 1), AssetType::Image);
    EXPECT_EQ(reader.GetAssetName(0, 1), "BrassAlbedo");
    EXPECT_EQ(reader.GetAssetType(2, 0), AssetType::TransformNode);
    EXPECT_EQ(reader.GetAssetName(2, 0), "");
    EXPECT_EQ(reader.GetAssetName(2, 1), "Hand_R");
    EXPECT_EQ(reader.GetAssetType(2, 2), AssetType::Camera);

    // The material payload decodes in place from an aligned buffer.
    const CpkgCompressionScheme scheme = reader.GetAssetCompression(0, 0);
    EXPECT_EQ(scheme.Codec, CpkgCodec::None);
    EXPECT_EQ(scheme.StoredSize, scheme.RawSize);
    std::vector<PayloadBlock> blocks(size_t(scheme.RawSize / kCpkgPayloadAlignment + 1));
    const std::span<std::byte> payload = AsSpan(blocks, size_t(scheme.RawSize));
    ASSERT_EQ(reader.ReadAsset(0, 0, payload), Result::Success);

    const CpkgMaterialData *pMaterial = nullptr;
    ASSERT_EQ(CpkgGetPayloadHeader(std::span<const std::byte>(payload), &pMaterial), Result::Success);
    EXPECT_EQ(pMaterial->BaseColorFactor[0], 1.0f);
    std::span<const char> albedo;
    ASSERT_EQ(CpkgResolveSpan<CpkgMaterialData>(payload, pMaterial->AlbedoTexture, &albedo), Result::Success);
    EXPECT_EQ(std::string_view(albedo.data(), albedo.size()), "BrassAlbedo");

    // An opaque payload reads back byte for byte.
    const CpkgCompressionScheme imageScheme = reader.GetAssetCompression(0, 1);
    std::vector<std::byte> image(size_t(imageScheme.RawSize));
    ASSERT_EQ(reader.ReadAsset(0, 1, image), Result::Success);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char *>(image.data()), image.size()), "not really a png");

    CpkgCameraData camera;
    ASSERT_EQ(reader.GetAssetCompression(2, 2).RawSize, sizeof(camera));
    ASSERT_EQ(reader.ReadAsset(2, 2, std::as_writable_bytes(std::span(&camera, 1))), Result::Success);
    EXPECT_EQ(camera.NodeIndex, 1);
    EXPECT_EQ(camera.FarClip, 1000.0f);

    EXPECT_EQ(reader.Close(), Result::Success);
}

//------------------------------------------------------------------------------------------------
TEST_F(CanvasPkgFile, NameDirectories)
{
    WriteSample("sample.cpkg");

    CCpkgNameDirectory bundles;
    CCpkgNameDirectory goblin;
    CCpkgNameDirectory images;
    {
        CCpkgReader reader;
        ASSERT_EQ(reader.Open(FileName("sample.cpkg").c_str()), Result::Success);
        ASSERT_EQ(reader.BuildBundleDirectory(&bundles), Result::Success);
        ASSERT_EQ(reader.BuildAssetDirectory(2, &goblin), Result::Success);
        ASSERT_EQ(reader.BuildAssetDirectory(0, &images, AssetType::Image), Result::Success);
        EXPECT_EQ(reader.BuildAssetDirectory(3, &goblin), Result::InvalidArg);
        EXPECT_EQ(reader.BuildBundleDirectory(nullptr), Result::InvalidArg);
    }

    // Directories outlive the reader that built them.
    EXPECT_EQ(bundles.Find("Materials"), 0u);
    EXPECT_EQ(bundles.Find("Empty"), 1u);
    EXPECT_EQ(bundles.Find("Goblin"), 2u);
    EXPECT_EQ(bundles.Find("goblin"), UINT32_MAX);

    EXPECT_EQ(goblin.Find("Hand_R"), 1u);
    EXPECT_EQ(goblin.Find(""), UINT32_MAX);

    EXPECT_EQ(images.Find("BrassAlbedo"), 1u);
    EXPECT_EQ(images.Find("Brass"), UINT32_MAX);
}

//------------------------------------------------------------------------------------------------
TEST_F(CanvasPkgFile, EmptyPackage)
{
    {
        CCpkgWriter writer;
        ASSERT_EQ(writer.Open(FileName("empty.cpkg").c_str(), "Nothing"), Result::Success);
        ASSERT_EQ(writer.Close(), Result::Success);
    }

    CCpkgReader reader;
    ASSERT_EQ(reader.Open(FileName("empty.cpkg").c_str()), Result::Success);
    EXPECT_EQ(reader.GetPackageName(), "Nothing");
    EXPECT_EQ(reader.GetBundleCount(), 0u);
}

//------------------------------------------------------------------------------------------------
TEST_F(CanvasPkgFile, NonAsciiFileName)
{
    // Two-byte, two-byte, and four-byte UTF-8 sequences.
    const std::filesystem::path path = m_Dir / std::u8string(u8"Grüße-\U0001F600.cpkg");
    {
        CCpkgWriter writer;
        ASSERT_EQ(writer.Open(ToUtf8(path).c_str(), "Nothing"), Result::Success);
        ASSERT_EQ(writer.Close(), Result::Success);
    }
    EXPECT_TRUE(std::filesystem::exists(path));

    CCpkgReader reader;
    ASSERT_EQ(reader.Open(ToUtf8(path).c_str()), Result::Success);
    EXPECT_EQ(reader.GetPackageName(), "Nothing");
}

//------------------------------------------------------------------------------------------------
TEST_F(CanvasPkgFile, DeterministicOutput)
{
    WriteSample("a.cpkg");
    WriteSample("b.cpkg");
    EXPECT_EQ(ReadBytes(PathOf("a.cpkg")), ReadBytes(PathOf("b.cpkg")));
}

//------------------------------------------------------------------------------------------------
TEST_F(CanvasPkgFile, WriterOpenRefusesExistingFile)
{
    const std::byte existing[] = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };
    WriteBytes(PathOf("existing.cpkg"), existing);

    CCpkgWriter writer;
    EXPECT_EQ(writer.Open(FileName("existing.cpkg").c_str(), "Sample"), Result::AlreadyExists);
    EXPECT_EQ(ReadBytes(PathOf("existing.cpkg")).size(), 3u);
    EXPECT_EQ(writer.Close(), Result::NotOpen);
}

//------------------------------------------------------------------------------------------------
TEST_F(CanvasPkgFile, WriterCallsWhenNotOpen)
{
    CCpkgWriter writer;
    const std::byte data[] = { std::byte{ 0 } };
    EXPECT_EQ(writer.BeginBundle("B"), Result::NotOpen);
    EXPECT_EQ(writer.AppendAsset(AssetType::Camera, "", CpkgCodec::None, data), Result::NotOpen);
    EXPECT_EQ(writer.EndBundle(), Result::NotOpen);
    EXPECT_EQ(writer.Close(), Result::NotOpen);
}

//------------------------------------------------------------------------------------------------
TEST_F(CanvasPkgFile, WriterArgumentRules)
{
    CpkgCameraData camera;
    const std::span<const std::byte> data = std::as_bytes(std::span(&camera, 1));

    CCpkgWriter writer;
    EXPECT_EQ(writer.Open(nullptr, "Sample"), Result::InvalidArg);
    EXPECT_EQ(writer.Open(FileName("rules.cpkg").c_str(), ""), Result::InvalidArg);
    EXPECT_EQ(writer.Open(FileName("rules.cpkg").c_str(), "A/B"), Result::InvalidArg);
    EXPECT_FALSE(std::filesystem::exists(PathOf("rules.cpkg")));

    ASSERT_EQ(writer.Open(FileName("rules.cpkg").c_str(), "Sample"), Result::Success);
    EXPECT_EQ(writer.Open(FileName("other.cpkg").c_str(), "Sample"), Result::InvalidArg);

    // Call order with no bundle open.
    EXPECT_EQ(writer.AppendAsset(AssetType::Camera, "", CpkgCodec::None, data), Result::InvalidArg);
    EXPECT_EQ(writer.EndBundle(), Result::InvalidArg);

    // Bundle names.
    EXPECT_EQ(writer.BeginBundle(""), Result::InvalidArg);
    EXPECT_EQ(writer.BeginBundle("A/B"), Result::InvalidArg);
    EXPECT_EQ(writer.BeginBundle(std::string_view("A\0B", 3)), Result::InvalidArg);
    ASSERT_EQ(writer.BeginBundle("naïve"), Result::Success);  // non-ASCII names are accepted

    // Call order and arguments with a bundle open.
    EXPECT_EQ(writer.BeginBundle("C"), Result::InvalidArg);
    EXPECT_EQ(writer.Close(), Result::InvalidArg);
    EXPECT_EQ(writer.AppendAsset(AssetType::Unknown, "", CpkgCodec::None, data), Result::InvalidArg);
    EXPECT_EQ(writer.AppendAsset(AssetType(99), "", CpkgCodec::None, data), Result::InvalidArg);
    EXPECT_EQ(writer.AppendAsset(AssetType::Camera, "", CpkgCodec(7), data), Result::InvalidArg);
    EXPECT_EQ(writer.AppendAsset(AssetType::Camera, "", CpkgCodec::None, {}), Result::InvalidArg);
    EXPECT_EQ(writer.AppendAsset(AssetType::Camera, "A/B", CpkgCodec::None, data), Result::InvalidArg);
    EXPECT_EQ(writer.AppendAsset(AssetType::Material, "", CpkgCodec::None, data), Result::InvalidArg);  // a shared asset is named

    ASSERT_EQ(writer.AppendAsset(AssetType::Camera, "Cam", CpkgCodec::None, data), Result::Success);
    ASSERT_EQ(writer.AppendAsset(AssetType::Camera, "", CpkgCodec::None, data), Result::Success);
    ASSERT_EQ(writer.AppendAsset(AssetType::Camera, "", CpkgCodec::None, data), Result::Success);
    EXPECT_EQ(writer.AppendAsset(AssetType::Light, "Cam", CpkgCodec::None, data), Result::InvalidArg);  // unique across types
    ASSERT_EQ(writer.EndBundle(), Result::Success);

    EXPECT_EQ(writer.BeginBundle("naïve"), Result::InvalidArg);  // unique within the package
    ASSERT_EQ(writer.BeginBundle("C"), Result::Success);
    ASSERT_EQ(writer.AppendAsset(AssetType::Camera, "Cam", CpkgCodec::None, data), Result::Success);  // names are per bundle
    ASSERT_EQ(writer.EndBundle(), Result::Success);
    ASSERT_EQ(writer.Close(), Result::Success);

    // Rejected calls left nothing behind.
    CCpkgReader reader;
    ASSERT_EQ(reader.Open(FileName("rules.cpkg").c_str()), Result::Success);
    ASSERT_EQ(reader.GetBundleCount(), 2u);
    EXPECT_EQ(reader.GetBundleName(0), "naïve");
    EXPECT_EQ(reader.GetAssetCount(0), 3u);
    EXPECT_EQ(reader.GetAssetCount(1), 1u);
    EXPECT_FALSE(std::filesystem::exists(PathOf("other.cpkg")));
}

//------------------------------------------------------------------------------------------------
TEST_F(CanvasPkgFile, DestroyingOpenWriterDeletesFile)
{
    {
        CCpkgWriter writer;
        ASSERT_EQ(writer.Open(FileName("abandoned.cpkg").c_str(), "Sample"), Result::Success);
        ASSERT_EQ(writer.BeginBundle("B"), Result::Success);
        EXPECT_TRUE(std::filesystem::exists(PathOf("abandoned.cpkg")));
    }
    EXPECT_FALSE(std::filesystem::exists(PathOf("abandoned.cpkg")));
}

//------------------------------------------------------------------------------------------------
TEST_F(CanvasPkgFile, ReaderCallsAndSentinels)
{
    CCpkgReader reader;
    std::byte buffer[64];
    CCpkgNameDirectory directory;

    // Not open.
    EXPECT_EQ(reader.Close(), Result::NotOpen);
    EXPECT_EQ(reader.ReadAsset(0, 0, buffer), Result::NotOpen);
    EXPECT_EQ(reader.BuildBundleDirectory(&directory), Result::NotOpen);
    EXPECT_EQ(reader.BuildAssetDirectory(0, &directory), Result::NotOpen);
    EXPECT_EQ(reader.GetPackageName(), "");
    EXPECT_EQ(reader.GetBundleCount(), 0u);
    EXPECT_EQ(reader.Open(nullptr), Result::InvalidArg);
    EXPECT_EQ(reader.Open(FileName("missing.cpkg").c_str()), Result::NotFound);

    WriteSample("sample.cpkg");
    ASSERT_EQ(reader.Open(FileName("sample.cpkg").c_str()), Result::Success);
    EXPECT_EQ(reader.Open(FileName("sample.cpkg").c_str()), Result::InvalidArg);
    EXPECT_EQ(reader.GetPackageName(), "Sample");

    // Out-of-range indices.
    EXPECT_EQ(reader.GetBundleName(3), "");
    EXPECT_EQ(reader.GetAssetCount(3), 0u);
    EXPECT_EQ(reader.GetAssetType(0, 2), AssetType::Unknown);
    EXPECT_EQ(reader.GetAssetType(3, 0), AssetType::Unknown);
    EXPECT_EQ(reader.GetAssetCompression(0, 2).RawSize, 0u);
    EXPECT_EQ(reader.GetAssetName(0, 2), "");
    EXPECT_EQ(reader.ReadAsset(0, 2, buffer), Result::InvalidArg);
    EXPECT_EQ(reader.ReadAsset(3, 0, buffer), Result::InvalidArg);

    // A buffer smaller than RawSize.
    const uint64_t rawSize = reader.GetAssetCompression(0, 1).RawSize;
    EXPECT_EQ(reader.ReadAsset(0, 1, std::span(buffer, size_t(rawSize - 1))), Result::InvalidArg);
    EXPECT_EQ(reader.ReadAsset(0, 1, std::span(buffer, size_t(rawSize))), Result::Success);

    EXPECT_EQ(reader.Close(), Result::Success);
    EXPECT_EQ(reader.GetBundleCount(), 0u);
}

//------------------------------------------------------------------------------------------------
TEST_F(CanvasPkgFile, ReaderRejectsDamagedFiles)
{
    WriteSample("sample.cpkg");
    const std::vector<std::byte> original = ReadBytes(PathOf("sample.cpkg"));
    ASSERT_EQ(OpenBytes(original), Result::Success);

    const CpkgFileHeader header = ReadAt<CpkgFileHeader>(original, 0);
    const uint64_t bundle0 = header.BundleTableOffset;
    const uint64_t bundle2 = header.BundleTableOffset + 2 * sizeof(CpkgBundleTableEntry);
    const uint64_t asset0 = ReadAt<CpkgBundleTableEntry>(original, bundle0).AssetTableOffset;
    const uint64_t goblinAsset1 = ReadAt<CpkgBundleTableEntry>(original, bundle2).AssetTableOffset + sizeof(CpkgAssetTableEntry);
    const uint64_t fileSize = original.size();

    auto expectHeader = [&](Result expected, std::function<void(CpkgFileHeader &)> patch)
    {
        std::vector<std::byte> bytes = original;
        PatchAt<CpkgFileHeader>(bytes, 0, patch);
        EXPECT_EQ(OpenBytes(bytes), expected);
    };
    auto expectBundle = [&](uint64_t offset, std::function<void(CpkgBundleTableEntry &)> patch)
    {
        std::vector<std::byte> bytes = original;
        PatchAt<CpkgBundleTableEntry>(bytes, offset, patch);
        EXPECT_EQ(OpenBytes(bytes), Result::CorruptedData);
    };
    auto expectAsset = [&](uint64_t offset, std::function<void(CpkgAssetTableEntry &)> patch)
    {
        std::vector<std::byte> bytes = original;
        PatchAt<CpkgAssetTableEntry>(bytes, offset, patch);
        EXPECT_EQ(OpenBytes(bytes), Result::CorruptedData);
    };

    // Not a package.
    EXPECT_EQ(OpenBytes(std::span(original).first(sizeof(CpkgFileHeader) - 1)), Result::DataNotRecognized);
    EXPECT_EQ(OpenBytes({}), Result::DataNotRecognized);
    expectHeader(Result::DataNotRecognized, [](CpkgFileHeader &h) { h.Magic[3] = 'X'; });

    // Unsupported version.
    expectHeader(Result::OutOfRange, [](CpkgFileHeader &h) { h.Version = kMinSupportedVersion - 1; });
    expectHeader(Result::OutOfRange, [](CpkgFileHeader &h) { h.Version = kMaxSupportedVersion + 1; });

    // File header extents. A zero BundleTableOffset is what an unclosed writer leaves.
    expectHeader(Result::CorruptedData, [](CpkgFileHeader &h) { h.BundleTableOffset = 0; });
    expectHeader(Result::CorruptedData, [](CpkgFileHeader &h) { h.PackageNameLength = 0; });
    expectHeader(Result::CorruptedData, [](CpkgFileHeader &h) { h.PackageNameOffset = 0; });
    expectHeader(Result::CorruptedData, [&](CpkgFileHeader &h) { h.PackageNameOffset = fileSize - 2; });
    expectHeader(Result::CorruptedData, [](CpkgFileHeader &h) { h.PackageNameOffset = UINT64_MAX; });
    expectHeader(Result::CorruptedData, [](CpkgFileHeader &h) { h.BundleCount = UINT32_MAX; });
    expectHeader(Result::CorruptedData, [&](CpkgFileHeader &h) { h.BundleTableOffset = fileSize; });
    expectHeader(Result::CorruptedData, [](CpkgFileHeader &h) { h.BundleNameBlobSize = UINT64_MAX; });
    expectHeader(Result::CorruptedData, [&](CpkgFileHeader &h) { h.BundleNameBlobOffset = fileSize; });
    expectHeader(Result::CorruptedData, [](CpkgFileHeader &h) { h.BundleNameBlobOffset = 4; });

    // Bundle table entries.
    expectBundle(bundle0, [](CpkgBundleTableEntry &e) { e.NameLength = 0; });
    expectBundle(bundle0, [&](CpkgBundleTableEntry &e) { e.NameOffset = header.BundleNameBlobSize; });
    expectBundle(bundle0, [](CpkgBundleTableEntry &e) { e.NameOffset = UINT64_MAX; });
    expectBundle(bundle0, [](CpkgBundleTableEntry &e) { e.AssetCount = UINT32_MAX; });
    expectBundle(bundle0, [](CpkgBundleTableEntry &e) { e.AssetTableOffset = 0; });
    expectBundle(bundle0, [&](CpkgBundleTableEntry &e) { e.AssetNameBlobOffset = fileSize; });
    expectBundle(bundle0, [](CpkgBundleTableEntry &e) { e.AssetNameBlobSize = UINT64_MAX; });

    // Asset table entries.
    expectAsset(asset0, [](CpkgAssetTableEntry &e) { e.NameLength = 100; });
    expectAsset(asset0, [](CpkgAssetTableEntry &e) { e.NameOffset = UINT64_MAX; });
    expectAsset(goblinAsset1 - sizeof(CpkgAssetTableEntry), [](CpkgAssetTableEntry &e) { e.NameOffset = 1; });  // unnamed with an offset
    expectAsset(asset0, [](CpkgAssetTableEntry &e) { e.Type = AssetType::Unknown; });
    expectAsset(asset0, [](CpkgAssetTableEntry &e) { e.Type = AssetType(99); });
    expectAsset(asset0, [](CpkgAssetTableEntry &e) { e.Compression.Codec = CpkgCodec(7); });
    expectAsset(asset0, [](CpkgAssetTableEntry &e) { e.Compression.RawSize += 1; });
    expectAsset(asset0, [](CpkgAssetTableEntry &e) { e.PayloadOffset = 0; });
    expectAsset(goblinAsset1, [&](CpkgAssetTableEntry &e) { e.PayloadOffset = fileSize - 1; });
    expectAsset(goblinAsset1, [](CpkgAssetTableEntry &e) { e.Compression.StoredSize = e.Compression.RawSize = UINT64_MAX; });
}
