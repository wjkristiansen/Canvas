//================================================================================================
// CpkgUnpackTest - Tests for cpkg's unpack command
//================================================================================================

#include "pch.h"
#include "CanvasPkg.h"
#include "CpkgTool.h"
#include "SchemaCheck.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    // Writes text to path as is, with no line-ending translation.
    void WriteText(const std::string &path, std::string_view text)
    {
        std::ofstream file(std::filesystem::path(path), std::ios::binary);
        file.write(text.data(), std::streamsize(text.size()));
    }

    // The bytes of the file at path; empty when it cannot be read.
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

    // Runs cpkg with args, collecting its standard output and error into Err.
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

    // The paths of the files under folder, relative to it with '/' separators, sorted.
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

    // "0, 1, ...": count numbers, each number i written as text(i).
    template <class TText>
    std::string NumberList(size_t count, TText &&text)
    {
        std::string list;
        for (size_t i = 0; i < count; ++i)
            list += (i == 0 ? "" : ", ") + text(i);
        return list;
    }

    // Appends an asset whose header fill sets, given the payload builder for the spans it holds.
    template <class THeader, class TFill>
    void AppendAsset(CCpkgWriter &writer, AssetType type, std::string_view name, TFill &&fill)
    {
        CCpkgPayloadBuilder<THeader> builder;
        THeader header;
        fill(builder, header);
        ASSERT_EQ(builder.SetHeader(header), Result::Success);
        ASSERT_EQ(writer.AppendAsset(type, name, CpkgCodec::None, builder.GetPayload()), Result::Success);
    }

    template <class THeader>
    CpkgSpan<char> Text(CCpkgPayloadBuilder<THeader> &builder, std::string_view text)
    {
        CpkgSpan<char> span;
        EXPECT_EQ(builder.Append(std::span<const char>(text.data(), text.size()), &span), Result::Success);
        return span;
    }
}

//------------------------------------------------------------------------------------------------
// Runs each test in its own empty current directory, so paths in diagnostics are relative to it.
class CpkgUnpack : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo *pInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        m_Dir = std::filesystem::temp_directory_path() / (std::string("CpkgUnpack_") + pInfo->name());
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

    // Unpacks package into folder, checks the files against the schema and the standard form, and
    // rebuilds them, expecting the rebuilt package to hold the same bytes.
    void ExpectRoundTrip(const std::string &package, const std::string &folder)
    {
        const RunResult unpacked = RunCpkg({ "unpack", package, "-o", folder + "/levels.cpkg.json" });
        EXPECT_EQ(unpacked.Err, "");
        ASSERT_EQ(unpacked.Status, CpkgTool::kExitSuccess);
        for (const std::string &file : FilesUnder(folder))
        {
            if (file.ends_with(".json"))
                EXPECT_TRUE(MatchesSchema(CanvasSchema::PackageSource, folder + "/" + file)) << file;
        }
        const RunResult checked = RunCpkg({ "format", folder + "/levels.cpkg.json", "--check" });
        EXPECT_EQ(checked.Err, "");
        EXPECT_EQ(checked.Status, CpkgTool::kExitSuccess);

        const RunResult rebuilt = RunCpkg({ "build", folder + "/levels.cpkg.json", "-o", folder + ".cpkg" });
        EXPECT_EQ(rebuilt.Err, "");
        ASSERT_EQ(rebuilt.Status, CpkgTool::kExitSuccess);
        EXPECT_EQ(ReadBytes(folder + ".cpkg"), ReadBytes(package));
    }

private:
    std::filesystem::path m_Dir;
    std::filesystem::path m_PreviousDir;
};

//------------------------------------------------------------------------------------------------
TEST_F(CpkgUnpack, WritesSourceThatBuildsThePackage)
{
    // 100 vertices and 100 keys: more than 256 numbers, so they go to binary array files.
    const std::string positions = NumberList(300, [](size_t i) { return std::to_string(i % 3 == 0 ? i / 3 : 0); });
    const std::string normals = NumberList(300, [](size_t i) { return std::string(i % 3 == 2 ? "1" : "0"); });
    const std::string times = NumberList(100, [](size_t i) { return std::to_string(i) + ".5"; });
    const std::string values = NumberList(300, [](size_t i) { return std::to_string(i) + ".25"; });

    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Looks",
      "materials": {
        "Brass": { "baseColor": [0.9, 0.7, 0.3, 1], "emissive": [0.1, 0.2, 0.3], "roughness": 0.35,
                   "textures": { "albedo": "Others/Textures/BrassAlbedo" },
                   "displacement": { "map": "Others/Textures/BrassHeight", "scale": 0.02 } }
      },
      "meshes": {
        "Big": { "bounds": { "min": [-1, -1, -1], "max": [1, 1, 1] }, "groups": [ { "material": "Brass", "positions": [)" + positions
        + R"(], "normals": [)" + normals + R"(] } ] },
        "Small": { "topology": "patchList4CP", "bounds": "none", "groups": [ { "positions": [-0.0, 1e-8, 3e38], "normals": [0, 0, 1], "uv0": [0.1, 0.2] } ] }
      },
      "skyboxes": { "Day": { "image": "Others/Skies/Day", "orientation": { "degrees": [0, 0, 30] }, "intensity": 1.5 } },
      "exports": ["Lamp"],
      "nodes": [ { "name": "Lamp", "light": { "type": "point", "color": [1, 0.5, 0.25], "enabled": false, "range": 20, "shadowResolution": 512 } } ]
    },
    {
      "name": "Rig",
      "skeletons": { "Skeleton": { "bones": [ { "name": "Hips" }, { "name": "Hand", "parent": "Hips", "rest": { "rotation": [0, 0, 0.70710678, 0.70710678] } } ] } },
      "skins": { "Skin": { "skeleton": "Skeleton", "bones": ["Hips"], "bindPoses": [ { "translation": [0, 0, 1] } ] } },
      "meshes": { "Body": { "groups": [ { "positions": [0, 0, 0], "normals": [0, 0, 1], "boneIndices": [0, 0, 0, 0], "boneWeights": [1, 0, 0, 0] } ] } },
      "skeletalAnimations": {
        "Stride": { "skeleton": "Skeleton", "tracks": [ { "bone": "Hips", "property": "translation", "times": [)" + times
        + R"(], "values": [)" + values + R"(], "interpolation": "step" } ] }
      },
      "nodes": [
        { "name": "Goblin", "mobility": "movable", "skeletonInstance": { "skeleton": "Skeleton" },
          "children": [
            { "name": "Grip", "bone": "Hand", "children": [ { "name": "Blade", "meshInstances": [ { "mesh": "Body", "skin": "Skin" } ] } ] },
            { "name": "Twin", "skeletonInstance": { "skeleton": "Skeleton" },
              "meshInstances": [ { "mesh": "Body", "skin": "Skin", "skeletonInstance": "Goblin" } ], "camera": { "fovAngle": 1.2 } }
          ] }
      ],
      "clips": {
        "Walk": { "skeletalAnimations": [ { "skeletonInstance": "Goblin", "animation": "Stride" } ] },
        "Spin": { "duration": 2, "tracks": [ { "node": "Goblin", "property": "rotation", "times": [0, 1], "values": [0, 0, 0, 1, 0, 0, 1, 0],
                  "interpolation": ["cubic", "linear"], "tangents": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] } ] },
        "Far": { "duration": 3, "skeletalAnimations": [ { "skeletonInstance": "Twin", "animation": "Others/Rigs/Wave" } ] }
      },
      "exports": ["Grip"]
    },
    {
      "name": "Camp",
      "placements": [ { "name": "Goblin1", "bundle": "Rig", "mobility": "movable", "translation": [12, 4, 0] }, { "bundle": "Others/Trees" } ],
      "importNodes": [ { "name": "Armed", "placement": "Goblin1", "export": "Grip", "placements": [ { "bundle": "Looks", "scale": [2, 2, 2] } ] } ],
      "meshFields": [ { "name": "Grass", "mesh": "Looks/Small", "cullDistance": 60, "transforms": [ {}, { "translation": [1, 2, 0] } ] } ]
    },
    {
      "name": "Arena",
      "nodes": [ { "name": "Overview", "translation": [0, -10, 5], "camera": {} } ],
      "world": { "regions": [ { "region": "Camp", "bounds": { "min": [0, 0, -10], "max": [64, 64, 50] } } ],
                 "skyboxes": ["Looks/Day", "Others/Skies/Night"], "defaultSkybox": "Others/Skies/Night", "activeCamera": "Overview",
                 "backgroundColor": [0.1, 0.2, 0.3, 1] }
    },
    { "name": "Empty" }
  ]
}
)");
    const RunResult built = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(built.Err, "");
    ASSERT_EQ(built.Status, CpkgTool::kExitSuccess);

    ExpectRoundTrip("levels.cpkg", "out");

    // A folder per bundle, holding its bundle file and its binary array files.
    const std::vector<std::string> files = {
        "Arena/Arena.cbundle.json", "Camp/Camp.cbundle.json", "Empty/Empty.cbundle.json", "Looks/Big.mesh.bin",
        "Looks/Looks.cbundle.json", "Rig/Rig.cbundle.json", "Rig/Stride.anim.bin", "levels.cpkg.json",
    };
    EXPECT_EQ(FilesUnder("out"), files);
    EXPECT_EQ(ReadBytes("out/Looks/Big.mesh.bin").size(), 600 * sizeof(float));
    EXPECT_EQ(ReadBytes("out/Rig/Stride.anim.bin").size(), 400 * sizeof(float));

    EXPECT_EQ(ReadBytes("out/levels.cpkg.json"), R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    { "include": "Looks/Looks.cbundle.json" },
    { "include": "Rig/Rig.cbundle.json" },
    { "include": "Camp/Camp.cbundle.json" },
    { "include": "Arena/Arena.cbundle.json" },
    { "include": "Empty/Empty.cbundle.json" }
  ]
}
)");

    // Defaults are omitted, numbers are written in their shortest form, rotations as
    // quaternions, and each array is a range of the binary array file.
    EXPECT_EQ(ReadBytes("out/Looks/Looks.cbundle.json"), R"({
  "formatVersion": 1,
  "bundle": {
    "name": "Looks",
    "materials": {
      "Brass": {
        "baseColor": [0.9, 0.7, 0.3, 1],
        "emissive": [0.1, 0.2, 0.3],
        "roughness": 0.35,
        "textures": { "albedo": "Others/Textures/BrassAlbedo" },
        "displacement": { "map": "Others/Textures/BrassHeight", "scale": 0.02 }
      }
    },
    "meshes": {
      "Big": {
        "bounds": { "min": [-1, -1, -1], "max": [1, 1, 1] },
        "groups": [
          {
            "material": "Brass",
            "positions": { "file": "Big.mesh.bin", "count": 100 },
            "normals": { "file": "Big.mesh.bin", "offset": 1200 }
          }
        ]
      },
      "Small": {
        "topology": "patchList4CP",
        "bounds": "none",
        "groups": [{ "positions": [-0.0, 1e-08, 3e+38], "normals": [0, 0, 1], "uv0": [0.1, 0.2] }]
      }
    },
    "skyboxes": {
      "Day": {
        "image": "Others/Skies/Day",
        "orientation": [0, 0, 0.25881904, 0.9659258],
        "intensity": 1.5
      }
    },
    "nodes": [
      {
        "name": "Lamp",
        "light": {
          "type": "point",
          "color": [1, 0.5, 0.25],
          "enabled": false,
          "range": 20,
          "shadowResolution": 512
        }
      }
    ],
    "exports": ["Lamp"]
  }
}
)");

    // A mesh instance binds the nearest skeleton instance unless it names another.
    const std::string rig = ReadBytes("out/Rig/Rig.cbundle.json");
    EXPECT_NE(rig.find(R"("meshInstances": [{ "mesh": "Body", "skin": "Skin" }])"), std::string::npos) << rig;
    EXPECT_NE(rig.find(R"("meshInstances": [{ "mesh": "Body", "skin": "Skin", "skeletonInstance": "Goblin" }])"), std::string::npos) << rig;
    EXPECT_NE(rig.find(R"("times": { "file": "Stride.anim.bin", "count": 100 })"), std::string::npos) << rig;
    EXPECT_NE(rig.find(R"("Walk": { "skeletalAnimations": [{ "skeletonInstance": "Goblin", "animation": "Stride" }] })"), std::string::npos) << rig;
    EXPECT_NE(rig.find(R"("duration": 3)"), std::string::npos) << rig;

    // Unpacking what the source builds writes the same source.
    ExpectRoundTrip("out.cpkg", "again");
    for (const std::string &file : files)
        EXPECT_EQ(ReadBytes("again/" + file), ReadBytes("out/" + file)) << file;
}

TEST_F(CpkgUnpack, NamesFilesForAnyName)
{
    const std::string positions = NumberList(300, [](size_t i) { return std::to_string(i); });
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    { "name": "a:b", "meshes": { "Big*": { "groups": [ { "positions": [)" + positions + R"(], "normals": [)" + positions + R"(] } ] } } },
    { "name": "CON" },
    { "name": "nul.x." },
    { "name": "Rig" },
    { "name": "rig" },
    { "name": "100%" }
  ]
}
)");
    const RunResult built = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(built.Err, "");
    ASSERT_EQ(built.Status, CpkgTool::kExitSuccess);

    ExpectRoundTrip("levels.cpkg", "out");
    const std::vector<std::string> files = {
        "%43ON/%43ON.cbundle.json", "%6Eul.x%2E/%6Eul.x..cbundle.json", "100%25/100%25.cbundle.json", "Rig/Rig.cbundle.json",
        "a%3Ab/Big%2A.mesh.bin", "a%3Ab/a%3Ab.cbundle.json", "levels.cpkg.json", "rig~2/rig.cbundle.json",
    };
    EXPECT_EQ(FilesUnder("out"), files);
}

// The file system decides which names collide: NTFS and APFS ignore the case of letters beyond
// ASCII, here U+00C4 and U+00E4, so the second takes a suffix there.
TEST_F(CpkgUnpack, NamesCollideAsTheFileSystemCompares)
{
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [ { "name": "Ärger" }, { "name": "ärger" } ]
}
)");
    ASSERT_EQ(RunCpkg({ "build", "levels.cpkg.json" }).Status, CpkgTool::kExitSuccess);

    ExpectRoundTrip("levels.cpkg", "out");
#if defined(_WIN32) || defined(__APPLE__)
    const std::vector<std::string> files = {
        "levels.cpkg.json", "\xC3\x84rger/\xC3\x84rger.cbundle.json", "\xC3\xA4rger~2/\xC3\xA4rger.cbundle.json",
    };
#else
    const std::vector<std::string> files = {
        "levels.cpkg.json", "\xC3\x84rger/\xC3\x84rger.cbundle.json", "\xC3\xA4rger/\xC3\xA4rger.cbundle.json",
    };
#endif
    EXPECT_EQ(FilesUnder("out"), files);
}

TEST_F(CpkgUnpack, ReplacesFilesOnlyWithForce)
{
    const char *pSource = R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [ { "name": "Looks", "materials": { "Plain": {} } } ]
}
)";
    WriteText("levels.cpkg.json", pSource);
    ASSERT_EQ(RunCpkg({ "build", "levels.cpkg.json" }).Status, CpkgTool::kExitSuccess);

    // The default output is the .cpkg file's name with .json added: here, the source.
    const RunResult refused = RunCpkg({ "unpack", "levels.cpkg" });
    EXPECT_EQ(refused.Err, "levels.cpkg.json: error: the file exists; --force replaces it\n");
    EXPECT_EQ(refused.Status, CpkgTool::kExitError);
    EXPECT_EQ(ReadBytes("levels.cpkg.json"), pSource);
    EXPECT_FALSE(std::filesystem::exists("Looks"));

    const RunResult forced = RunCpkg({ "unpack", "levels.cpkg", "--force" });
    EXPECT_EQ(forced.Err, "");
    ASSERT_EQ(forced.Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(FilesUnder("."), (std::vector<std::string>{ "Looks/Looks.cbundle.json", "levels.cpkg", "levels.cpkg.json" }));

    const std::string package = ReadBytes("levels.cpkg");
    ASSERT_EQ(RunCpkg({ "build", "levels.cpkg.json" }).Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(ReadBytes("levels.cpkg"), package);
}

TEST_F(CpkgUnpack, ReportsWhatNoSourceExpresses)
{
    {
        CCpkgWriter writer;
        ASSERT_EQ(writer.Open("odd.cpkg", "Levels"), Result::Success);
        ASSERT_EQ(writer.BeginBundle("Camp"), Result::Success);
        AppendAsset<CpkgImageData>(writer, AssetType::Image, "Sky", [](auto &, CpkgImageData &image)
        {
            image.Encoding = CpkgImageEncoding::Png;
            image.Dimension = CpkgImageDimension::Texture2D;
            image.Width = image.Height = image.Depth = image.ArraySize = image.MipLevels = 1;
        });
        AppendAsset<CpkgTransformNodeData>(writer, AssetType::TransformNode, "Fire", [](auto &, CpkgTransformNodeData &) {});
        AppendAsset<CpkgPlacementData>(writer, AssetType::Placement, "", [](auto &builder, CpkgPlacementData &placement)
        {
            placement.Bundle = Text(builder, "Others/Goblin");
        });
        AppendAsset<CpkgImportNodeData>(writer, AssetType::ImportNode, "Hand", [](auto &builder, CpkgImportNodeData &node)
        {
            node.PlacementIndex = 2;
            node.ExportName = Text(builder, "Hand_R");
        });
        AppendAsset<CpkgLightData>(writer, AssetType::Light, "", [](auto &, CpkgLightData &light)
        {
            light.NodeIndex = 1;
            light.Color[3] = 0.5f;
        });
        ASSERT_EQ(writer.EndBundle(), Result::Success);
        ASSERT_EQ(writer.Close(), Result::Success);
    }

    const RunResult odd = RunCpkg({ "unpack", "odd.cpkg" });
    EXPECT_EQ(odd.Err,
        "odd.cpkg: error: bundle \"Camp\", asset 0 (Image \"Sky\"): cpkg cannot unpack images yet\n"
        "odd.cpkg: error: bundle \"Camp\", asset 3 (ImportNode \"Hand\"): its placement is unnamed, and a source names the placement an import node refers to\n"
        "odd.cpkg: error: bundle \"Camp\", asset 4 (Light): its color has an alpha other than 1, which no source expresses\n");
    EXPECT_EQ(odd.Status, CpkgTool::kExitError);

    // A source orders shared assets by type, so a mesh before a material is out of build order.
    // Only building the source finds it.
    {
        CCpkgWriter writer;
        ASSERT_EQ(writer.Open("order.cpkg", "Levels"), Result::Success);
        ASSERT_EQ(writer.BeginBundle("Looks"), Result::Success);
        AppendAsset<CpkgMeshData>(writer, AssetType::MeshData, "Tri", [](auto &builder, CpkgMeshData &mesh)
        {
            const CpkgVertexPosition position = { { 0, 0, 0 } };
            const CpkgVertexNormal normal = { { 0, 0, 1 } };
            CpkgMeshDataGroup group;
            EXPECT_EQ(builder.Append(std::span(&position, 1), &group.Positions), Result::Success);
            EXPECT_EQ(builder.Append(std::span(&normal, 1), &group.Normals), Result::Success);
            EXPECT_EQ(builder.Append(std::span(&group, 1), &mesh.Groups), Result::Success);
            mesh.LocalBounds = { { 0, 0, 0 }, { 0, 0, 0 } };
        });
        AppendAsset<CpkgMaterialData>(writer, AssetType::Material, "Plain", [](auto &, CpkgMaterialData &) {});
        ASSERT_EQ(writer.EndBundle(), Result::Success);
        ASSERT_EQ(writer.Close(), Result::Success);
    }

    const RunResult order = RunCpkg({ "unpack", "order.cpkg", "-o", "out/order.cpkg.json" });
    EXPECT_EQ(order.Err, "order.cpkg: error: bundle \"Looks\", asset 0 (MeshData \"Tri\"): it is not in the order a build appends the bundle's assets\n");
    EXPECT_EQ(order.Status, CpkgTool::kExitError);

    // Unpack wrote nothing, not even the output's folder.
    EXPECT_EQ(FilesUnder("."), (std::vector<std::string>{ "odd.cpkg", "order.cpkg" }));
    EXPECT_FALSE(std::filesystem::exists("out"));
}

TEST_F(CpkgUnpack, UsageErrors)
{
    const char *pHelp = "Run 'cpkg --help' for usage.\n";
    EXPECT_EQ(RunCpkg({ "unpack" }).Err, std::string("cpkg: error: unpack: missing argument <package.cpkg>\n") + pHelp);
    EXPECT_EQ(RunCpkg({ "unpack", "levels.json" }).Err, std::string("cpkg: error: unpack: expected a .cpkg file: levels.json\n") + pHelp);
    EXPECT_EQ(RunCpkg({ "unpack", "levels.cpkg", "-o", "levels.json" }).Err,
        std::string("cpkg: error: unpack: expected a .cpkg.json output file: levels.json\n") + pHelp);
    EXPECT_EQ(RunCpkg({ "unpack", "levels.cpkg" }).Err, "levels.cpkg: error: file not found\n");
}
