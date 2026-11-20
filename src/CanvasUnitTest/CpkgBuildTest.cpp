//================================================================================================
// CpkgBuildTest - Tests for cpkg's Validate and Write stages, through the build command
//================================================================================================

#include "pch.h"
#include "CanvasPkg.h"
#include "CpkgTool.h"

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

    // What a cpkg run returned and printed.
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

        std::span<const std::byte> Bytes() const { return std::as_bytes(std::span(m_Blocks)).first(m_Size); }

        template <class THeader>
        const THeader &Header() const
        {
            const THeader *pHeader = nullptr;
            EXPECT_EQ(CpkgGetPayloadHeader(Bytes(), &pHeader), Result::Success);
            return *pHeader;
        }

        template <class THeader, class T>
        std::span<const T> Elements(const CpkgSpan<T> &span) const
        {
            std::span<const T> elements;
            EXPECT_EQ((CpkgResolveSpan<THeader>(Bytes(), span, &elements)), Result::Success);
            return elements;
        }

        template <class THeader>
        std::string Text(const CpkgSpan<char> &span) const
        {
            const std::span<const char> chars = Elements<THeader>(span);
            return std::string(chars.begin(), chars.end());
        }

    private:
        struct alignas(kCpkgPayloadAlignment) Block
        {
            std::byte Bytes[kCpkgPayloadAlignment];
        };
        std::vector<Block> m_Blocks;
        size_t m_Size = 0;
    };
}

//------------------------------------------------------------------------------------------------
// Runs each test in its own empty current directory, so paths in diagnostics are relative to it.
class CpkgBuild : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo *pInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        m_Dir = std::filesystem::temp_directory_path() / (std::string("CpkgBuild_") + pInfo->name());
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

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, WritesSharedAssets)
{
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Looks",
      "materials": {
        "Brass": { "baseColor": [0.9, 0.7, 0.3, 1], "emissive": [0.1, 0.2, 0.3], "roughness": 0.35, "metallic": 1,
                   "textures": { "albedo": "Textures/Metals/BrassAlbedo" },
                   "displacement": { "map": "Textures/Metals/BrassHeight", "scale": 0.02 } },
        "Plain": {}
      },
      "skyboxes": { "Day": { "image": "Textures/Skies/Day", "orientation": [0, 0, 0, 2], "intensity": 1.5 } },
      "meshes": {
        "Tri": { "groups": [
          { "material": "Brass", "positions": [0, 0, 0, 1, 0, 0, 1, 1, 0], "normals": [0, 0, 1, 0, 0, 1, 0, 0, 1] },
          { "positions": [-1, 2, -3], "normals": [0, 0, 1], "uv0": [0.5, 0.5] } ] },
        "Boxed": { "bounds": { "min": [-1, -1, -1], "max": [1, 1, 1] }, "groups": [ { "positions": [0, 0, 0], "normals": [0, 0, 1] } ] },
        "Unbounded": { "bounds": "none", "groups": [ { "positions": [0, 0, 0], "normals": [0, 0, 1] } ] }
      }
    },
    {
      "name": "Rig",
      "skeletons": { "Goblin": { "bones": [
        { "name": "Hips", "rest": { "translation": [0, 0, 1] } },
        { "name": "Spine", "parent": "Hips", "rest": { "translation": [0, 0, 0.5], "rotation": [0, 0, 0, 3] } } ] } },
      "skins": {
        "GoblinSkin": { "skeleton": "Goblin", "bones": ["Spine", "Hips"], "bindPoses": [ { "translation": [0, 0, 1.5] }, { "translation": [0, 0, 1] } ] },
        "BorrowedSkin": { "skeleton": "Others/Rigs/Troll", "bones": ["Root"], "bindPoses": [ { "rotation": [0, 0, 0, 2], "scale": [2, 2, 2] } ] }
      },
      "skeletalAnimations": { "Walk": { "skeleton": "Goblin", "tracks": [
        { "bone": "Hips", "property": "rotation", "times": [0, 1], "values": [0, 0, 0, 2, 0, 0, 0, 4] },
        { "bone": "Spine", "property": "translation", "times": [0, 1], "values": [0, 0, 0, 0, 0, 1], "interpolation": "step" },
        { "bone": "Spine", "property": "scale", "times": [0, 1], "values": [1, 1, 1, 2, 2, 2],
          "interpolation": ["cubic", "linear"], "tangents": [0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0] } ] } }
    },
    { "name": "Empty" }
  ]
}
)");

    const RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(result.Err, "");
    ASSERT_EQ(result.Status, CpkgTool::kExitSuccess);

    CCpkgReader reader;
    ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
    EXPECT_EQ(reader.GetPackageName(), "Levels");
    ASSERT_EQ(reader.GetBundleCount(), 3u);
    EXPECT_EQ(reader.GetBundleName(0), "Looks");
    EXPECT_EQ(reader.GetBundleName(2), "Empty");
    EXPECT_EQ(reader.GetAssetCount(2), 0u);

    // Canonical order: skyboxes, materials, then meshes, each in source order.
    ASSERT_EQ(reader.GetAssetCount(0), 6u);
    const std::pair<AssetType, std::string_view> looks[] = {
        { AssetType::Skybox, "Day" }, { AssetType::Material, "Brass" }, { AssetType::Material, "Plain" },
        { AssetType::MeshData, "Tri" }, { AssetType::MeshData, "Boxed" }, { AssetType::MeshData, "Unbounded" },
    };
    for (uint32_t i = 0; i < 6; ++i)
    {
        EXPECT_EQ(reader.GetAssetType(0, i), looks[i].first) << i;
        EXPECT_EQ(reader.GetAssetName(0, i), looks[i].second) << i;
        EXPECT_EQ(reader.GetAssetCompression(0, i).Codec, CpkgCodec::None) << i;
    }

    {
        const CPayload payload(reader, 0, 0);
        const CpkgSkyboxData &skybox = payload.Header<CpkgSkyboxData>();
        EXPECT_EQ(payload.Text<CpkgSkyboxData>(skybox.Image), "Textures/Skies/Day");
        EXPECT_EQ(skybox.Orientation[3], 1.0f);
        EXPECT_EQ(skybox.Intensity, 1.5f);
    }
    {
        const CPayload payload(reader, 0, 1);
        const CpkgMaterialData &brass = payload.Header<CpkgMaterialData>();
        EXPECT_EQ(brass.BaseColorFactor[1], 0.7f);
        EXPECT_EQ(brass.EmissiveFactor[2], 0.3f);
        EXPECT_EQ(brass.EmissiveFactor[3], 0.0f);
        EXPECT_EQ(brass.RoughMetalAOFactor[0], 0.35f);
        EXPECT_EQ(brass.RoughMetalAOFactor[1], 1.0f);
        EXPECT_EQ(brass.RoughMetalAOFactor[2], 1.0f);
        EXPECT_EQ(payload.Text<CpkgMaterialData>(brass.AlbedoTexture), "Textures/Metals/BrassAlbedo");
        EXPECT_EQ(brass.NormalTexture.Count, 0u);
        EXPECT_EQ(brass.HasDisplacement, 1u);
        EXPECT_EQ(payload.Text<CpkgMaterialData>(brass.Displacement.DisplacementMap), "Textures/Metals/BrassHeight");
        EXPECT_EQ(brass.Displacement.MapScale, 0.02f);
        EXPECT_EQ(brass.Displacement.MaxTessFactor, 32.0f);
    }
    {
        const CPayload payload(reader, 0, 2);
        const CpkgMaterialData &plain = payload.Header<CpkgMaterialData>();
        EXPECT_EQ(plain.BaseColorFactor[0], 1.0f);
        EXPECT_EQ(plain.RoughMetalAOFactor[0], 1.0f);
        EXPECT_EQ(plain.HasDisplacement, 0u);
        EXPECT_EQ(plain.Displacement.DisplacementMap.Count, 0u);
    }
    {
        const CPayload payload(reader, 0, 3);
        const CpkgMeshData &tri = payload.Header<CpkgMeshData>();
        EXPECT_EQ(tri.Topology, GfxPrimitiveTopology::TriangleList);
        EXPECT_EQ(tri.LocalBounds.Min[0], -1.0f);
        EXPECT_EQ(tri.LocalBounds.Max[1], 2.0f);
        EXPECT_EQ(tri.LocalBounds.Min[2], -3.0f);
        EXPECT_EQ(tri.LocalBounds.Max[2], 0.0f);
        const std::span<const CpkgMeshDataGroup> groups = payload.Elements<CpkgMeshData>(tri.Groups);
        ASSERT_EQ(groups.size(), 2u);
        EXPECT_EQ(payload.Text<CpkgMeshData>(groups[0].Material), "Brass");
        const std::span<const CpkgVertexPosition> positions = payload.Elements<CpkgMeshData>(groups[0].Positions);
        ASSERT_EQ(positions.size(), 3u);
        EXPECT_EQ(positions[2].XYZ[1], 1.0f);
        EXPECT_EQ(groups[0].Normals.Count, 3u);
        EXPECT_EQ(groups[0].UV0.Count, 0u);
        EXPECT_EQ(groups[1].Material.Count, 0u);
        EXPECT_EQ(payload.Elements<CpkgMeshData>(groups[1].UV0)[0].UV[1], 0.5f);
    }
    {
        const CPayload payload(reader, 0, 4);
        EXPECT_EQ(payload.Header<CpkgMeshData>().LocalBounds.Max[0], 1.0f);
        const CPayload unbounded(reader, 0, 5);
        EXPECT_EQ(unbounded.Header<CpkgMeshData>().LocalBounds.Min[0], FLT_MAX);
        EXPECT_EQ(unbounded.Header<CpkgMeshData>().LocalBounds.Max[0], -FLT_MAX);
    }

    ASSERT_EQ(reader.GetAssetCount(1), 4u);
    EXPECT_EQ(reader.GetAssetType(1, 0), AssetType::Skeleton);
    EXPECT_EQ(reader.GetAssetType(1, 1), AssetType::MeshSkin);
    EXPECT_EQ(reader.GetAssetName(1, 2), "BorrowedSkin");
    EXPECT_EQ(reader.GetAssetType(1, 3), AssetType::SkeletalAnimation);
    {
        const CPayload payload(reader, 1, 0);
        const std::span<const CpkgSkeletonBone> bones = payload.Elements<CpkgSkeletonData>(payload.Header<CpkgSkeletonData>().Bones);
        ASSERT_EQ(bones.size(), 2u);
        EXPECT_EQ(bones[0].ParentIndex, -1);
        EXPECT_EQ(bones[1].ParentIndex, 0);
        EXPECT_EQ(payload.Text<CpkgSkeletonData>(bones[1].Name), "Spine");
        EXPECT_EQ(bones[1].RestTransform.Translation[2], 0.5f);
        EXPECT_EQ(bones[1].RestTransform.Rotation[3], 1.0f);
        EXPECT_EQ(bones[0].RestTransform.Scale[0], 1.0f);
    }
    {
        const CPayload payload(reader, 1, 1);
        const CpkgMeshSkinData &skin = payload.Header<CpkgMeshSkinData>();
        EXPECT_EQ(payload.Text<CpkgMeshSkinData>(skin.Skeleton), "Goblin");
        const std::span<const CpkgSpan<char>> bones = payload.Elements<CpkgMeshSkinData>(skin.Bones);
        ASSERT_EQ(bones.size(), 2u);
        EXPECT_EQ(payload.Text<CpkgMeshSkinData>(bones[0]), "Spine");
        const std::span<const CpkgTransform> poses = payload.Elements<CpkgMeshSkinData>(skin.BindPoses);
        ASSERT_EQ(poses.size(), 2u);
        EXPECT_EQ(poses[0].Translation[2], 1.5f);
        EXPECT_EQ(poses[0].Rotation[3], 1.0f);
        EXPECT_EQ(poses[0].Scale[0], 1.0f);
        EXPECT_EQ(poses[1].Translation[2], 1.0f);
    }
    {
        // Bind pose rotations are normalized.
        const CPayload payload(reader, 1, 2);
        const std::span<const CpkgTransform> poses = payload.Elements<CpkgMeshSkinData>(payload.Header<CpkgMeshSkinData>().BindPoses);
        ASSERT_EQ(poses.size(), 1u);
        EXPECT_EQ(poses[0].Rotation[3], 1.0f);
        EXPECT_EQ(poses[0].Scale[1], 2.0f);
    }
    {
        const CPayload payload(reader, 1, 3);
        const CpkgSkeletalAnimationData &walk = payload.Header<CpkgSkeletalAnimationData>();
        EXPECT_EQ(payload.Text<CpkgSkeletalAnimationData>(walk.Skeleton), "Goblin");
        const std::span<const CpkgBoneTrack> tracks = payload.Elements<CpkgSkeletalAnimationData>(walk.Tracks);
        ASSERT_EQ(tracks.size(), 3u);

        // Rotation keys are normalized; every key is linear, so no interpolations are stored.
        EXPECT_EQ(payload.Text<CpkgSkeletalAnimationData>(tracks[0].Bone), "Hips");
        EXPECT_EQ(tracks[0].Property, CpkgAnimationProperty::Rotation);
        const std::span<const float> rotations = payload.Elements<CpkgSkeletalAnimationData>(tracks[0].Values);
        ASSERT_EQ(rotations.size(), 8u);
        EXPECT_EQ(rotations[3], 1.0f);
        EXPECT_EQ(rotations[7], 1.0f);
        EXPECT_EQ(tracks[0].Interpolations.Count, 0u);
        EXPECT_EQ(tracks[0].Tangents.Count, 0u);

        // One interpolation for every key is stored per key.
        const std::span<const AnimationInterpolation> steps = payload.Elements<CpkgSkeletalAnimationData>(tracks[1].Interpolations);
        EXPECT_EQ(steps.size(), 2u);
        EXPECT_EQ(steps[1], AnimationInterpolation::Step);

        EXPECT_EQ(tracks[2].Interpolations.Count, 2u);
        EXPECT_EQ(tracks[2].Tangents.Count, 12u);
        EXPECT_EQ(payload.Elements<CpkgSkeletalAnimationData>(tracks[2].Tangents)[3], 1.0f);
    }

    // -o names the output, and a second build replaces it.
    EXPECT_EQ(RunCpkg({ "build", "levels.cpkg.json", "-o", "out.cpkg" }).Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(RunCpkg({ "build", "levels.cpkg.json", "--output", "out.cpkg" }).Status, CpkgTool::kExitSuccess);
    // Builds of the same source are byte for byte the same.
    reader.Close();
    auto bytes = [](const char *path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    EXPECT_FALSE(bytes("out.cpkg").empty());
    EXPECT_EQ(bytes("out.cpkg"), bytes("levels.cpkg"));
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ReportsValidationErrorsInDocumentOrder)
{
    WriteText("parts.cbundle.json", R"({
  "formatVersion": 1,
  "bundle": { "name": "Looks", "materials": { "Steel": { "textures": { "normal": "Sky" } } }, "meshes": { "Steel": { "groups": [ { "positions": [0, 0, 0] } ] } } }
})");
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Looks",
      "images": { "Sky": { "cubemap": { "+X": "a.png", "-X": "a.png", "+Y": "a.png", "-Y": "a.png", "+Z": "a.png", "-Z": "a.png" } } },
      "materials": { "Brass": { "textures": { "albedo": "Sky" } } },
      "skyboxes": { "Day": { "image": "Brass", "orientation": [0, 0, 0, 0] }, "Night": { "image": "Missing/Sky" } },
      "meshes": {
        "Tri": { "topology": "patchList4CP", "bounds": { "min": [0, 0, 1], "max": [1, 1, 0] },
                 "groups": [ { "material": "Plastic", "positions": [0, 0, 0, 1, 0, 0], "normals": [0, 0, 1], "boneWeights": [1, 0, 0, 0, 1, 0, 0, 0] },
                             { "positions": [], "normals": [] } ] }
      }
    },
    {
      "name": "Rig",
      "skeletons": { "Goblin": { "bones": [ { "name": "Hips", "parent": "Spine" }, { "name": "Spine" }, { "name": "Spine", "parent": "Tail" } ] } },
      "skins": {
        "Skin": { "skeleton": "Goblin", "bones": ["Hips", "Head"], "bindPoses": [ {} ] },
        "Borrowed": { "skeleton": "Others/Rigs/Troll", "bones": ["Root"], "bindPoses": [ { "rotation": [0, 0, 0, 0] } ] }
      },
      "skeletalAnimations": { "Walk": { "skeleton": "Goblin", "tracks": [
        { "bone": "Hips", "property": "rotation", "times": [1, 0], "values": [0, 0, 0, 1, 0, 0, 0, 0] },
        { "bone": "Hips", "property": "rotation", "times": [0], "values": [0, 0, 0, 1, 0], "interpolation": ["cubic", "step"] },
        { "bone": "Neck", "property": "scale", "times": [], "values": [], "tangents": [1, 1, 1] } ] } }
    },
    { "include": "parts.cbundle.json" }
  ]
}
)");

    const RunResult result = RunCpkg({ "validate", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:8:57: error: \"Sky\" is not a 2D image with one array slice [/bundles/0/materials/Brass/textures/albedo]\n"
        "levels.cpkg.json:9:39: error: \"Brass\" is a material, not an image [/bundles/0/skyboxes/Day/image]\n"
        "levels.cpkg.json:9:63: error: expected a non-zero quaternion [/bundles/0/skyboxes/Day/orientation]\n"
        "levels.cpkg.json:9:99: error: the package has no bundle \"Missing\" [/bundles/0/skyboxes/Night/image]\n"
        "levels.cpkg.json:11:56: error: expected \"min\" <= \"max\" on every axis [/bundles/0/meshes/Tri/bounds]\n"
        "levels.cpkg.json:12:30: error: a group of a \"patchList4CP\" mesh needs \"uv0\" [/bundles/0/meshes/Tri/groups/0]\n"
        "levels.cpkg.json:12:30: error: a group of a skinned mesh needs \"boneIndices\" and \"boneWeights\" [/bundles/0/meshes/Tri/groups/0]\n"
        "levels.cpkg.json:12:44: error: the bundle has no asset \"Plastic\" [/bundles/0/meshes/Tri/groups/0/material]\n"
        "levels.cpkg.json:12:99: error: expected 2 vertices, as \"positions\" holds; found 1 [/bundles/0/meshes/Tri/groups/0/normals]\n"
        "levels.cpkg.json:13:30: error: a group of a \"patchList4CP\" mesh needs \"uv0\" [/bundles/0/meshes/Tri/groups/1]\n"
        "levels.cpkg.json:13:30: error: a group of a skinned mesh needs \"boneIndices\" and \"boneWeights\" [/bundles/0/meshes/Tri/groups/1]\n"
        "levels.cpkg.json:13:45: error: expected at least one vertex [/bundles/0/meshes/Tri/groups/1/positions]\n"
        "levels.cpkg.json:18:73: error: \"Spine\" is not an earlier bone [/bundles/1/skeletons/Goblin/bones/0/parent]\n"
        "levels.cpkg.json:18:115: error: the skeleton already has a bone named \"Spine\" [/bundles/1/skeletons/Goblin/bones/2/name]\n"
        "levels.cpkg.json:18:134: error: the skeleton has no bone \"Tail\" [/bundles/1/skeletons/Goblin/bones/2/parent]\n"
        "levels.cpkg.json:20:59: error: the skeleton \"Goblin\" has no bone \"Head\" [/bundles/1/skins/Skin/bones/1]\n"
        "levels.cpkg.json:20:81: error: expected 2 bind poses, one per bone; found 1 [/bundles/1/skins/Skin/bindPoses]\n"
        "levels.cpkg.json:21:104: error: expected a non-zero quaternion [/bundles/1/skins/Borrowed/bindPoses/0/rotation]\n"
        "levels.cpkg.json:24:60: error: expected nondecreasing times; key 1 is earlier than key 0 [/bundles/1/skeletalAnimations/Walk/tracks/0/times]\n"
        "levels.cpkg.json:24:78: error: expected non-zero quaternions; key 1 is zero [/bundles/1/skeletalAnimations/Walk/tracks/0/values]\n"
        "levels.cpkg.json:25:9: error: a track with a \"cubic\" key needs \"tangents\" [/bundles/1/skeletalAnimations/Walk/tracks/1]\n"
        "levels.cpkg.json:25:19: error: the animation already has a \"rotation\" track for \"Hips\" [/bundles/1/skeletalAnimations/Walk/tracks/1/bone]\n"
        "levels.cpkg.json:25:75: error: expected 4 numbers, 4 for each of 1 key; found 5 [/bundles/1/skeletalAnimations/Walk/tracks/1/values]\n"
        "levels.cpkg.json:25:109: error: expected 1 interpolation, one per key; found 2 [/bundles/1/skeletalAnimations/Walk/tracks/1/interpolation]\n"
        "levels.cpkg.json:26:19: error: the skeleton \"Goblin\" has no bone \"Neck\" [/bundles/1/skeletalAnimations/Walk/tracks/2/bone]\n"
        "levels.cpkg.json:26:57: error: expected at least one key [/bundles/1/skeletalAnimations/Walk/tracks/2/times]\n"
        "levels.cpkg.json:26:87: error: expected no tangents, since no key is \"cubic\"; found 3 [/bundles/1/skeletalAnimations/Walk/tracks/2/tangents]\n"
        "parts.cbundle.json:3:23: error: the package already has a bundle named \"Looks\" [/bundle/name]\n"
        "parts.cbundle.json:3:82: error: the bundle has no asset \"Sky\" [/bundle/materials/Steel/textures/normal]\n"
        "parts.cbundle.json:3:107: error: the bundle already uses the name \"Steel\" [/bundle/meshes/Steel]\n"
        "parts.cbundle.json:3:130: error: a group needs \"normals\" [/bundle/meshes/Steel/groups/0]\n");
    EXPECT_FALSE(std::filesystem::exists("levels.cpkg"));
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ReportsMembersItCannotBuildYet)
{
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [ { "name": "Village", "nodes": [ { "name": "Well" } ], "exports": [] } ]
}
)");

    RunResult result = RunCpkg({ "validate", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(result.Err, "");

    result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:4:16: error: cpkg cannot build a bundle's \"nodes\" yet [/bundles/0]\n"
        "levels.cpkg.json:4:16: error: cpkg cannot build a bundle's \"exports\" yet [/bundles/0]\n");
    EXPECT_FALSE(std::filesystem::exists("levels.cpkg"));
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, UsageErrors)
{
    RunResult result = RunCpkg({ "build" });
    EXPECT_EQ(result.Status, CpkgTool::kExitUsage);
    EXPECT_EQ(result.Err, "cpkg: error: build: missing argument <package.cpkg.json>\nRun 'cpkg --help' for usage.\n");

    result = RunCpkg({ "build", "levels.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitUsage);
    EXPECT_EQ(result.Err, "cpkg: error: build: expected a .cpkg.json file: levels.json\nRun 'cpkg --help' for usage.\n");

    result = RunCpkg({ "build", "missing.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err, "missing.cpkg.json: error: file not found\n");
}
