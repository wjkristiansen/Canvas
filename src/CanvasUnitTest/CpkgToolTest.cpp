//================================================================================================
// CpkgToolTest - Tests for the cpkg command-line tool, run in-process
//================================================================================================

#include "pch.h"
#include "CanvasPkg.h"
#include "CpkgTool.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    struct RunResult
    {
        int Status = -1;
        std::string Out;
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
        result.Out = out.str();
        result.Err = err.str();
        return result;
    }

    std::span<const std::byte> AsBytes(std::string_view text)
    {
        return { reinterpret_cast<const std::byte *>(text.data()), text.size() };
    }

    template <class T>
    std::span<const std::byte> BytesOf(const T &value)
    {
        return std::as_bytes(std::span(&value, 1));
    }

    template <class T>
    void AppendString(CCpkgPayloadBuilder<T> &builder, std::string_view text, CpkgSpan<char> *pSpan)
    {
        ASSERT_EQ(builder.Append(std::span(text.data(), text.size()), pSpan), Result::Success);
    }
}

//------------------------------------------------------------------------------------------------
// Runs each test in its own empty current directory, so file paths on the command line and in
// diagnostics are relative to it.
class CpkgCli : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo *pInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        m_Dir = std::filesystem::temp_directory_path() / (std::string("CpkgTool_") + pInfo->name());
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

    // Writes "Sample": bundle "Materials" with a material and an image whose payload is not an
    // image's, an empty bundle "Empty", and bundle "Goblin" with named and unnamed local assets.
    void WriteSample(const char *fileName)
    {
        CCpkgWriter writer;
        ASSERT_EQ(writer.Open(fileName, "Sample"), Result::Success);

        ASSERT_EQ(writer.BeginBundle("Materials"), Result::Success);
        CCpkgPayloadBuilder<CpkgMaterialData> material;
        CpkgMaterialData brass;
        AppendString(material, "BrassAlbedo", &brass.AlbedoTexture);
        ASSERT_EQ(material.SetHeader(brass), Result::Success);
        m_MaterialSize = material.GetPayload().size();
        ASSERT_EQ(writer.AppendAsset(AssetType::Material, "Brass", CpkgCodec::None, material.GetPayload()), Result::Success);
        ASSERT_EQ(writer.AppendAsset(AssetType::Image, "BrassAlbedo", CpkgCodec::None, AsBytes("not really a png")), Result::Success);
        ASSERT_EQ(writer.EndBundle(), Result::Success);

        ASSERT_EQ(writer.BeginBundle("Empty"), Result::Success);
        ASSERT_EQ(writer.EndBundle(), Result::Success);

        ASSERT_EQ(writer.BeginBundle("Goblin"), Result::Success);
        CpkgTransformNodeData root;
        CpkgTransformNodeData hand;
        hand.ParentIndex = 0;
        hand.Mobility = CpkgMobility::Movable;
        hand.LocalTransform.Translation[0] = 0.5f;
        CpkgCameraData camera;
        camera.NodeIndex = 1;
        ASSERT_EQ(writer.AppendAsset(AssetType::TransformNode, "", CpkgCodec::None, BytesOf(root)), Result::Success);
        ASSERT_EQ(writer.AppendAsset(AssetType::TransformNode, "Hand_R", CpkgCodec::None, BytesOf(hand)), Result::Success);
        ASSERT_EQ(writer.AppendAsset(AssetType::Camera, "", CpkgCodec::None, BytesOf(camera)), Result::Success);
        ASSERT_EQ(writer.EndBundle(), Result::Success);

        ASSERT_EQ(writer.Close(), Result::Success);
    }

    std::filesystem::path m_Dir;
    std::filesystem::path m_PreviousDir;
    size_t m_MaterialSize = 0;
};

//------------------------------------------------------------------------------------------------
TEST_F(CpkgCli, PrintsVersion)
{
    const RunResult result = RunCpkg({ "--version" });
    EXPECT_EQ(result.Status, 0);
    EXPECT_EQ(result.Out.rfind("cpkg ", 0), 0u) << result.Out;
    EXPECT_TRUE(result.Out.ends_with("\n"));
    EXPECT_EQ(result.Err, "");
}

TEST_F(CpkgCli, PrintsHelp)
{
    RunResult result = RunCpkg({ "--help" });
    EXPECT_EQ(result.Status, 0);
    EXPECT_NE(result.Out.find("dump"), std::string::npos) << result.Out;
    EXPECT_NE(result.Out.find("--werror"), std::string::npos) << result.Out;
    EXPECT_EQ(result.Err, "");

    // Global options and commands are listed in the order cpkg declares them.
    const size_t help = result.Out.find("  --help, -h");
    const size_t werror = result.Out.find("  --werror");
    const size_t build = result.Out.find("  build ");
    const size_t format = result.Out.find("  format ");
    EXPECT_LT(help, werror) << result.Out;
    EXPECT_LT(werror, build) << result.Out;
    EXPECT_LT(build, format) << result.Out;
    EXPECT_EQ(result.Out.find(" \n"), std::string::npos) << result.Out;

    result = RunCpkg({ "dump", "-h" });
    EXPECT_EQ(result.Status, 0);
    EXPECT_NE(result.Out.find("--payloads"), std::string::npos) << result.Out;
    EXPECT_NE(result.Out.find("<package.cpkg>"), std::string::npos) << result.Out;
}

TEST_F(CpkgCli, InvalidCommandLines)
{
    const std::vector<std::vector<std::string>> commandLines = {
        {},
        { "frobnicate" },
        { "--frobnicate" },
        { "dump" },
        { "dump", "sample.cpkg.json" },
        { "dump", "sample.cpkg", "extra" },
        { "dump", "sample.cpkg", "--bundle" },
        { "dump", "sample.cpkg", "--frobnicate" },
    };
    for (const std::vector<std::string> &args : commandLines)
    {
        const RunResult result = RunCpkg(args);
        EXPECT_EQ(result.Status, 2) << result.Err;
        EXPECT_EQ(result.Out, "");
        EXPECT_EQ(result.Err.rfind("cpkg: error: ", 0), 0u) << result.Err;
    }

    EXPECT_EQ(RunCpkg({}).Err, "cpkg: error: missing command\nRun 'cpkg --help' for usage.\n");
    EXPECT_EQ(RunCpkg({ "biuld" }).Err, "cpkg: error: unknown command 'biuld'\nRun 'cpkg --help' for usage.\n");
    EXPECT_EQ(RunCpkg({ "dump", "sample.cpkg", "--bundle" }).Err,
        "cpkg: error: dump: missing value for option --bundle\nRun 'cpkg --help' for usage.\n");
    EXPECT_EQ(RunCpkg({ "dump" }).Err, "cpkg: error: dump: missing argument <package.cpkg>\nRun 'cpkg --help' for usage.\n");
    EXPECT_EQ(RunCpkg({ "dump", "a.txt" }).Err, "cpkg: error: dump: expected a .cpkg file: a.txt\nRun 'cpkg --help' for usage.\n");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgCli, DumpListsBundlesAndAssets)
{
    WriteSample("sample.cpkg");
    const RunResult result = RunCpkg({ "dump", "sample.cpkg" });
    EXPECT_EQ(result.Status, 0);
    EXPECT_EQ(result.Err, "");
    ASSERT_EQ(std::to_string(m_MaterialSize).size(), 3u);
    EXPECT_EQ(result.Out,
        "Package: \"Sample\"\n"
        "Version: 1\n"
        "Bundles: 3\n"
        "\n"
        "Bundle 0: \"Materials\" (2 assets)\n"
        "  0  Material  " + std::to_string(m_MaterialSize) + " bytes  \"Brass\"\n"
        "  1  Image      16 bytes  \"BrassAlbedo\"\n"
        "\n"
        "Bundle 1: \"Empty\" (0 assets)\n"
        "\n"
        "Bundle 2: \"Goblin\" (3 assets)\n"
        "  0  TransformNode  64 bytes\n"
        "  1  TransformNode  64 bytes  \"Hand_R\"\n"
        "  2  Camera         20 bytes\n");
}

TEST_F(CpkgCli, DumpOneBundle)
{
    WriteSample("sample.cpkg");
    RunResult result = RunCpkg({ "dump", "sample.cpkg", "--bundle", "Empty" });
    EXPECT_EQ(result.Status, 0);
    EXPECT_EQ(result.Out,
        "Package: \"Sample\"\n"
        "Version: 1\n"
        "Bundles: 3\n"
        "\n"
        "Bundle 1: \"Empty\" (0 assets)\n");

    result = RunCpkg({ "dump", "sample.cpkg", "--bundle", "empty" });
    EXPECT_EQ(result.Status, 1);
    EXPECT_EQ(result.Out, "");
    EXPECT_EQ(result.Err, "sample.cpkg: error: the package has no bundle named \"empty\"\n");
}

TEST_F(CpkgCli, DumpPayloads)
{
    WriteSample("sample.cpkg");
    const RunResult result = RunCpkg({ "dump", "sample.cpkg", "--bundle", "Goblin", "--payloads" });
    EXPECT_EQ(result.Status, 0);
    EXPECT_EQ(result.Err, "");
    EXPECT_EQ(result.Out,
        "Package: \"Sample\"\n"
        "Version: 1\n"
        "Bundles: 3\n"
        "\n"
        "Bundle 2: \"Goblin\" (3 assets)\n"
        "  0  TransformNode  64 bytes\n"
        "    ParentIndex: -1\n"
        "    Mobility: Static\n"
        "    ParentBone: \"\"\n"
        "    LocalTransform:\n"
        "      Scale: 1, 1, 1\n"
        "      Rotation: 0, 0, 0, 1\n"
        "      Translation: 0, 0, 0\n"
        "  1  TransformNode  64 bytes  \"Hand_R\"\n"
        "    ParentIndex: 0\n"
        "    Mobility: Movable\n"
        "    ParentBone: \"\"\n"
        "    LocalTransform:\n"
        "      Scale: 1, 1, 1\n"
        "      Rotation: 0, 0, 0, 1\n"
        "      Translation: 0.5, 0, 0\n"
        "  2  Camera         20 bytes\n"
        "    NodeIndex: 1\n"
        "    NearClip: 0.1\n"
        "    FarClip: 1000\n"
        "    FovAngle: 0.785398\n"
        "    ExposureStops: 0\n");
}

TEST_F(CpkgCli, DumpPayloadArrays)
{
    {
        CCpkgWriter writer;
        ASSERT_EQ(writer.Open("hero.cpkg", "Hero"), Result::Success);
        ASSERT_EQ(writer.BeginBundle("Hero"), Result::Success);

        CCpkgPayloadBuilder<CpkgSkeletonData> skeleton;
        CpkgSkeletonBone bones[2];
        AppendString(skeleton, "Root", &bones[0].Name);
        AppendString(skeleton, "Arm", &bones[1].Name);
        bones[1].ParentIndex = 0;
        bones[1].RestTransform.Rotation[2] = -0.25f;
        CpkgSkeletonData skeletonData;
        ASSERT_EQ(skeleton.Append(std::span(bones), &skeletonData.Bones), Result::Success);
        ASSERT_EQ(skeleton.SetHeader(skeletonData), Result::Success);
        ASSERT_EQ(writer.AppendAsset(AssetType::Skeleton, "Rig", CpkgCodec::None, skeleton.GetPayload()), Result::Success);

        CCpkgPayloadBuilder<CpkgMeshData> mesh;
        const CpkgVertexPosition positions[3] = {};
        const CpkgVertexNormal normals[3] = {};
        CpkgMeshDataGroup group;
        AppendString(mesh, "Lib/Brass", &group.Material);
        ASSERT_EQ(mesh.Append(std::span(positions), &group.Positions), Result::Success);
        ASSERT_EQ(mesh.Append(std::span(normals), &group.Normals), Result::Success);
        CpkgMeshData meshData;
        meshData.LocalBounds = { { -1.0f, 0.0f, -1.0f }, { 1.0f, 2.0f, 1.0f } };
        ASSERT_EQ(mesh.Append(std::span(&group, 1), &meshData.Groups), Result::Success);
        ASSERT_EQ(mesh.SetHeader(meshData), Result::Success);
        ASSERT_EQ(writer.AppendAsset(AssetType::MeshData, "Body", CpkgCodec::None, mesh.GetPayload()), Result::Success);

        CCpkgPayloadBuilder<CpkgMeshSkinData> skin;
        CpkgMeshSkinData skinData;
        CpkgSpan<char> boneNames[2];
        AppendString(skin, "Rig", &skinData.Skeleton);
        AppendString(skin, "Root", &boneNames[0]);
        AppendString(skin, "Arm \"left\"", &boneNames[1]);
        ASSERT_EQ(skin.Append(std::span(boneNames), &skinData.Bones), Result::Success);
        CpkgTransform poses[2];
        poses[1].Translation[2] = 1.5f;
        ASSERT_EQ(skin.Append(std::span(poses), &skinData.BindPoses), Result::Success);
        ASSERT_EQ(skin.SetHeader(skinData), Result::Success);
        ASSERT_EQ(writer.AppendAsset(AssetType::MeshSkin, "BodySkin", CpkgCodec::None, skin.GetPayload()), Result::Success);

        CCpkgPayloadBuilder<CpkgImageData> image;
        CpkgImageData imageData = { CpkgImageEncoding::Png, CpkgImageDimension::Texture2D, 4, 4, 1, 1, 1 };
        const uint8_t encoded[5] = { 1, 2, 3, 4, 5 };
        CpkgSpan<uint8_t> subresource;
        ASSERT_EQ(image.Append(std::span(encoded), &subresource), Result::Success);
        ASSERT_EQ(image.Append(std::span(&subresource, 1), &imageData.Images), Result::Success);
        ASSERT_EQ(image.SetHeader(imageData), Result::Success);
        ASSERT_EQ(writer.AppendAsset(AssetType::Image, "Skin", CpkgCodec::None, image.GetPayload()), Result::Success);

        CCpkgPayloadBuilder<CpkgExportListData> exports;
        CpkgExportListData exportData;
        const int32_t indices[2] = { 0, 2 };
        ASSERT_EQ(exports.Append(std::span(indices), &exportData.NodeIndices), Result::Success);
        ASSERT_EQ(exports.SetHeader(exportData), Result::Success);
        ASSERT_EQ(writer.AppendAsset(AssetType::ExportList, "", CpkgCodec::None, exports.GetPayload()), Result::Success);

        CpkgLightData light;
        light.Flags = LightFlags::Enabled | LightFlags::CastsShadows;
        ASSERT_EQ(writer.AppendAsset(AssetType::Light, "", CpkgCodec::None, BytesOf(light)), Result::Success);

        ASSERT_EQ(writer.EndBundle(), Result::Success);
        ASSERT_EQ(writer.Close(), Result::Success);
    }

    const RunResult result = RunCpkg({ "dump", "hero.cpkg", "--payloads" });
    EXPECT_EQ(result.Status, 0);
    EXPECT_EQ(result.Err, "");

    // Compare from the first payload field on; the asset lines hold the payload sizes.
    const size_t start = result.Out.find("    Bones: 2\n");
    ASSERT_NE(start, std::string::npos) << result.Out;
    std::string fields;
    std::istringstream lines(result.Out.substr(start));
    for (std::string line; std::getline(lines, line);)
        fields += line.rfind("    ", 0) == 0 ? line + "\n" : "--\n";
    EXPECT_EQ(fields,
        "    Bones: 2\n"
        "      [0]:\n"
        "        ParentIndex: -1\n"
        "        Name: \"Root\"\n"
        "        RestTransform:\n"
        "          Scale: 1, 1, 1\n"
        "          Rotation: 0, 0, 0, 1\n"
        "          Translation: 0, 0, 0\n"
        "      [1]:\n"
        "        ParentIndex: 0\n"
        "        Name: \"Arm\"\n"
        "        RestTransform:\n"
        "          Scale: 1, 1, 1\n"
        "          Rotation: 0, 0, -0.25, 1\n"
        "          Translation: 0, 0, 0\n"
        "--\n"
        "    Topology: TriangleList\n"
        "    LocalBounds:\n"
        "      Min: -1, 0, -1\n"
        "      Max: 1, 2, 1\n"
        "    Groups: 1\n"
        "      [0]:\n"
        "        Material: \"Lib/Brass\"\n"
        "        Positions: 3\n"
        "        Normals: 3\n"
        "        UV0: 0\n"
        "        Tangents: 0\n"
        "        BoneIndices: 0\n"
        "        BoneWeights: 0\n"
        "--\n"
        "    Skeleton: \"Rig\"\n"
        "    Bones: 2\n"
        "      [0]: \"Root\"\n"
        "      [1]: \"Arm \\\"left\\\"\"\n"
        "    BindPoses: 2\n"
        "      [0]:\n"
        "        Scale: 1, 1, 1\n"
        "        Rotation: 0, 0, 0, 1\n"
        "        Translation: 0, 0, 0\n"
        "      [1]:\n"
        "        Scale: 1, 1, 1\n"
        "        Rotation: 0, 0, 0, 1\n"
        "        Translation: 0, 0, 1.5\n"
        "--\n"
        "    Encoding: Png\n"
        "    Dimension: Texture2D\n"
        "    Width: 4\n"
        "    Height: 4\n"
        "    Depth: 1\n"
        "    ArraySize: 1\n"
        "    MipLevels: 1\n"
        "    Images: 1\n"
        "      [0]: 5 bytes\n"
        "--\n"
        "    NodeIndices: 0, 2\n"
        "--\n"
        "    NodeIndex: -1\n"
        "    Type: Point\n"
        "    Color: 1, 1, 1, 1\n"
        "    Intensity: 1\n"
        "    Flags: CastsShadows | Enabled\n"
        "    AttenuationConstant: 1\n"
        "    AttenuationLinear: 0\n"
        "    AttenuationQuadratic: 0\n"
        "    Range: 100\n"
        "    SpotInnerAngle: 0.785398\n"
        "    SpotOuterAngle: 1.047198\n"
        "    ShadowResolution: 0\n"
        "    ShadowConstantBias: 1e-04\n"
        "    ShadowSlopeScaleBias: 2\n"
        "    ShadowNormalOffset: 0.5\n");
}

TEST_F(CpkgCli, DumpReportsDamagedPayloads)
{
    {
        CCpkgWriter writer;
        ASSERT_EQ(writer.Open("damaged.cpkg", "Damaged"), Result::Success);
        ASSERT_EQ(writer.BeginBundle("Things"), Result::Success);
        ASSERT_EQ(writer.AppendAsset(AssetType::Image, "Short", CpkgCodec::None, AsBytes("not really a png")), Result::Success);
        CpkgTransformNodeData node;
        node.ParentBone = { 1000, 3 };
        ASSERT_EQ(writer.AppendAsset(AssetType::TransformNode, "Node", CpkgCodec::None, BytesOf(node)), Result::Success);
        CpkgCameraData camera;
        ASSERT_EQ(writer.AppendAsset(AssetType::Camera, "", CpkgCodec::None, BytesOf(camera)), Result::Success);
        ASSERT_EQ(writer.EndBundle(), Result::Success);
        ASSERT_EQ(writer.Close(), Result::Success);
    }

    const RunResult result = RunCpkg({ "dump", "damaged.cpkg", "--payloads" });
    EXPECT_EQ(result.Status, 1);
    EXPECT_EQ(result.Err,
        "damaged.cpkg: error: bundle \"Things\", asset 0 (Image): the payload is too small for its CpkgImageData header\n"
        "damaged.cpkg: error: bundle \"Things\", asset 1 (TransformNode): ParentBone breaks the payload layout rules\n");

    // Each asset is still listed, and an asset whose payload is whole still shows its fields.
    EXPECT_NE(result.Out.find("  0  Image          16 bytes  \"Short\"\n  1  TransformNode  64 bytes  \"Node\"\n  2  Camera         20 bytes\n    NodeIndex: -1\n"),
        std::string::npos) << result.Out;
}

TEST_F(CpkgCli, DumpReportsFilesItCannotRead)
{
    RunResult result = RunCpkg({ "dump", "missing.cpkg" });
    EXPECT_EQ(result.Status, 1);
    EXPECT_EQ(result.Out, "");
    EXPECT_EQ(result.Err, "missing.cpkg: error: file not found\n");

    std::filesystem::create_directories("sub");
    std::ofstream("sub/text.cpkg") << "This is not a package, but it is long enough to hold a header.";
    result = RunCpkg({ "dump", "sub/text.cpkg" });
    EXPECT_EQ(result.Status, 1);
    EXPECT_EQ(result.Err, "sub/text.cpkg: error: not a package file\n");

    // An absolute path is reported relative to the current directory.
    result = RunCpkg({ "dump", (std::filesystem::current_path() / "sub" / "text.cpkg").string() });
    EXPECT_EQ(result.Err, "sub/text.cpkg: error: not a package file\n");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgCli, DiagnosticsSeverityOptions)
{
    std::ostringstream stream;
    {
        CpkgTool::CDiagnostics diagnostics(stream, false, false);
        diagnostics.Report(CpkgTool::Severity::Warning, "a.cpkg.json", 3, 7, "unused key", "/bundles/0/x");
        diagnostics.Report(CpkgTool::Severity::Info, "a.cpkg.json", "built");
        EXPECT_EQ(diagnostics.GetErrorCount(), 0u);
    }
    EXPECT_EQ(stream.str(),
        "a.cpkg.json:3:7: warning: unused key [/bundles/0/x]\n"
        "a.cpkg.json: info: built\n");

    stream.str("");
    {
        CpkgTool::CDiagnostics diagnostics(stream, true, true);
        diagnostics.Report(CpkgTool::Severity::Warning, "a.cpkg.json", 3, 7, "unused key", "/x");
        diagnostics.Report(CpkgTool::Severity::Info, "a.cpkg.json", "built");
        EXPECT_EQ(diagnostics.GetErrorCount(), 1u);
    }
    EXPECT_EQ(stream.str(), "a.cpkg.json:3:7: error: unused key [/x]\n");

    stream.str("");
    {
        CpkgTool::CDiagnostics diagnostics(stream, false, true);
        diagnostics.Report(CpkgTool::Severity::Warning, "a.cpkg.json", "unused key");
        diagnostics.Report(CpkgTool::Severity::Error, "a.cpkg.json", "bad key");
        EXPECT_EQ(diagnostics.GetErrorCount(), 1u);
    }
    EXPECT_EQ(stream.str(), "a.cpkg.json: error: bad key\n");
}

TEST_F(CpkgCli, QuotedEscapes)
{
    EXPECT_EQ(CpkgTool::Quoted(""), "\"\"");
    EXPECT_EQ(CpkgTool::Quoted("a \"b\" \\c"), "\"a \\\"b\\\" \\\\c\"");
    EXPECT_EQ(CpkgTool::Quoted("\n\r\t"), "\"\\n\\r\\t\"");
    EXPECT_EQ(CpkgTool::Quoted(std::string_view("\x01\x1f", 2)), "\"\\u0001\\u001f\"");
    EXPECT_EQ(CpkgTool::Quoted("Gr\xc3\xbc\xc3\x9f" "e"), "\"Gr\xc3\xbc\xc3\x9f" "e\"");
}

TEST_F(CpkgCli, DumpNamesAnUnsupportedVersion)
{
    WriteSample("sample.cpkg");
    {
        std::fstream file("sample.cpkg", std::ios::in | std::ios::out | std::ios::binary);
        const uint32_t version = kMaxSupportedVersion + 1;
        file.seekp(offsetof(CpkgFileHeader, Version));
        file.write(reinterpret_cast<const char *>(&version), sizeof(version));
    }

    const RunResult result = RunCpkg({ "dump", "sample.cpkg" });
    EXPECT_EQ(result.Status, 1);
    EXPECT_EQ(result.Out, "");
    EXPECT_EQ(result.Err, "sample.cpkg: error: the package's format version " + std::to_string(kMaxSupportedVersion + 1)
        + " is not supported; this cpkg reads versions " + std::to_string(kMinSupportedVersion) + " to " + std::to_string(kMaxSupportedVersion) + "\n");
}
