//================================================================================================
// CpkgSourceTest - Tests for cpkg's Load stage, which reads package sources
//================================================================================================

#include "pch.h"
#include "CpkgTool.h"
#include "Source.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace CpkgTool;

namespace
{
    void WriteText(const std::string &path, std::string_view bytes)
    {
        std::ofstream file(std::filesystem::path(path), std::ios::binary);
        file.write(bytes.data(), std::streamsize(bytes.size()));
    }

    template <class T>
    void WriteBinary(const std::string &path, std::initializer_list<T> values)
    {
        WriteText(path, std::string_view(reinterpret_cast<const char *>(std::data(values)), values.size() * sizeof(T)));
    }

    struct LoadResult
    {
        bool Loaded = false;
        std::string Diagnostics;
    };

    LoadResult Load(const std::string &path, SourcePackage *pPackage)
    {
        std::ostringstream stream;
        CDiagnostics diagnostics(stream, false, false);
        LoadResult result;
        result.Loaded = LoadPackageSource(path, pPackage, diagnostics);
        result.Diagnostics = stream.str();
        return result;
    }

    int RunValidate(std::vector<std::string> args, std::string *pErr)
    {
        args.insert(args.begin(), { "cpkg", "validate" });
        std::vector<const char *> argv;
        for (const std::string &arg : args)
            argv.push_back(arg.c_str());
        std::ostringstream out;
        std::ostringstream err;
        const int status = CpkgTool::Run(int(argv.size()), argv.data(), out, err);
        *pErr = out.str() + err.str();
        return status;
    }
}

//------------------------------------------------------------------------------------------------
// Runs each test in its own empty current directory, so paths in diagnostics are relative to it.
class CpkgSource : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo *pInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        m_Dir = std::filesystem::temp_directory_path() / (std::string("CpkgSource_") + pInfo->name());
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
TEST_F(CpkgSource, LoadsEveryMember)
{
    std::filesystem::create_directories("parts");
    WriteBinary<float>("parts/positions.bin", { 0, 0, 0, 1, 0, 0, 1, 1, 0 });
    WriteText("levels.cpkg.json", R"({
  "$schema": "package-source.schema.json",
  "formatVersion": 1,
  "packageName": "Levels",
  "$comment": "Every member, once", "$extras": { "anything": [1, 2] },
  "bundles": [
    {
      "name": "Props",
      "images": {
        "Albedo": "art/albedo.png",
        "Sky": { "cubemap": { "+X": "px.png", "-X": "nx.png", "+Y": "py.png", "-Y": "ny.png", "+Z": "pz.png", "-Z": "nz.png" } },
        "Volume": { "dimension": "texture3D", "mipLevels": 2, "files": ["v0.png", "v1.png"] }
      },
      "materials": {
        "Brass": { "baseColor": [0.9, 0.7, 0.3, 1], "roughness": 0.35, "textures": { "albedo": "Albedo", "normal": "Shared/Metals/BrassNormal" },
                   "displacement": { "map": "Albedo", "scale": 2 } },
        "Plain": {}
      },
      "meshes": {
        "Tri": { "topology": "triangleList", "bounds": { "min": [0, 0, 0], "max": [1, 1, 0] },
                 "groups": [ { "material": "Brass", "positions": { "file": "parts/positions.bin" }, "normals": [0, 0, 1, 0, 0, 1, 0, 0, 1],
                               "boneIndices": [0, 1, 2, 3, 0, 0, 0, 0, 1, 1, 1, 1] } ] },
        "Empty": { "bounds": "none", "groups": [ { "positions": [] } ] }
      },
      "skeletons": { "Rig": { "bones": [ { "name": "Hips" }, { "name": "Spine", "parent": "Hips", "rest": { "translation": [0, 0, 0.12] } } ] } },
      "skins": { "Skin": { "skeleton": "Rig", "bones": ["Hips", "Spine"] } },
      "skeletalAnimations": {
        "Walk": { "skeleton": "Rig", "tracks": [ { "bone": "Hips", "property": "rotation", "times": [0, 1], "values": [0, 0, 0, 1, 0, 0, 0, 1],
                                                   "interpolation": ["step", "cubic"], "tangents": [] } ] }
      },
      "skyboxes": { "Day": { "image": "Sky", "orientation": { "degrees": [90, 0, 90] }, "intensity": 2 } },
      "nodes": [
        {
          "name": "Goblin", "mobility": "movable", "skeletonInstance": { "skeleton": "Rig" },
          "meshInstances": [ { "mesh": "Tri", "skin": "Skin" } ],
          "children": [
            { "name": "Hand", "bone": "Spine", "placements": [ { "bundle": "Weapons/Sword" } ] },
            { "name": "Eyes", "translation": [0.1, 0, 1.6], "camera": { "fovAngle": 1.2 } },
            { "name": "Lamp", "light": { "type": "spot", "color": [1, 0.5, 0], "castsShadows": true, "shadowResolution": 1024 } }
          ]
        }
      ],
      "placements": [ { "name": "Rider", "bundle": "Characters/Goblin", "mobility": "stationary", "rotation": [0, 0, 0, 2] } ],
      "meshFields": [ { "mesh": "Tri", "cullDistance": 60, "transforms": [ {}, { "translation": [3, 1, 0] } ] } ],
      "importNodes": [ { "name": "RiderHand", "placement": "Rider", "export": "Hand_R", "placements": [ { "bundle": "Sword" } ] } ],
      "clips": { "Wave": { "duration": 2, "tracks": [ { "node": "Eyes", "property": "scale", "times": [0], "values": [1, 1, 1] } ],
                           "skeletalAnimations": [ { "skeletonInstance": "Goblin", "animation": "Walk" } ] } },
      "exports": ["Hand", "RiderHand"],
      "world": { "regions": [ { "region": "Village", "bounds": { "min": [0, 0, -10], "max": [64, 64, 50] } } ],
                 "skyboxes": ["Day"], "defaultSkybox": "Day", "activeCamera": "Eyes", "backgroundColor": [0.1, 0.2, 0.3, 1] },
      "imports": [
        { "file": "art/goblin.fbx", "scale": 0.01, "nodes": false, "names": { "meshes": "G_{name}.mesh" },
          "rename": { "meshes": { "$comment": "from the artist", "Cube.001": "Body" } }, "replace": { "materials": { "Skin": "Shared/Skin" } },
          "clips": [ { "name": "Idle", "frames": [0, 59] }, { "name": "Run", "stack": "Take 001", "start": 1, "end": 2 } ],
          "edits": { "$extras": null, "Armature": { "mobility": "movable", "light": { "intensity": 3 }, "children": [ { "name": "Grip", "bone": "Hand_R" } ] } } }
      ]
    },
    { "include": "parts/village.cbundle.json" }
  ]
})");
    WriteText("parts/village.cbundle.json", R"({
  "formatVersion": 1,
  "bundle": { "name": "Village", "$comment": "kept apart", "materials": { "$comment": "metals", "Brass": { "metallic": 1 }, "$extras": [1] } }
})");

    SourcePackage package;
    const LoadResult result = Load("levels.cpkg.json", &package);
    EXPECT_EQ(result.Diagnostics, "");
    ASSERT_TRUE(result.Loaded);

    EXPECT_EQ(package.PackageName.Text, "Levels");
    ASSERT_EQ(package.Files.size(), 2u);
    EXPECT_EQ(package.Files[1]->Path, "parts/village.cbundle.json");
    ASSERT_EQ(package.Bundles.size(), 2u);

    const SourceBundle &props = package.Bundles[0];
    EXPECT_EQ(props.Name.Text, "Props");

    ASSERT_EQ(props.Images.size(), 3u);
    EXPECT_EQ(props.Images[0].Value.Form, SourceImageForm::File);
    EXPECT_EQ(props.Images[0].Value.Files[0].Path, "art/albedo.png");
    EXPECT_EQ(props.Images[1].Value.Form, SourceImageForm::Cubemap);
    EXPECT_EQ(props.Images[1].Value.ArraySize, 6u);
    ASSERT_EQ(props.Images[1].Value.Files.size(), 6u);
    EXPECT_EQ(props.Images[1].Value.Files[3].Written, "ny.png");
    EXPECT_EQ(props.Images[2].Value.Dimension, CpkgImageDimension::Texture3D);
    EXPECT_EQ(props.Images[2].Value.ArraySize, 1u);
    EXPECT_EQ(props.Images[2].Value.MipLevels, 2u);

    ASSERT_EQ(props.Materials.size(), 2u);
    const SourceMaterial &brass = props.Materials[0].Value;
    EXPECT_EQ(brass.Roughness, 0.35f);
    EXPECT_EQ(brass.Metallic, 0.0f);
    EXPECT_EQ(brass.BaseColor[2], 0.3f);
    ASSERT_TRUE(brass.Textures[size_t(SourceTextureRole::Normal)]);
    EXPECT_EQ(brass.Textures[size_t(SourceTextureRole::Normal)]->Text, "Shared/Metals/BrassNormal");
    EXPECT_FALSE(brass.Textures[size_t(SourceTextureRole::Emissive)]);
    ASSERT_TRUE(brass.Displacement);
    EXPECT_EQ(brass.Displacement->Scale, 2.0f);
    EXPECT_EQ(brass.Displacement->MaxTessFactor, 32.0f);
    EXPECT_EQ(props.Materials[1].Value.AmbientOcclusion, 1.0f);

    ASSERT_EQ(props.Meshes.size(), 2u);
    const SourceMesh &tri = props.Meshes[0].Value;
    EXPECT_EQ(tri.BoundsForm, SourceBoundsForm::Declared);
    EXPECT_EQ(tri.Bounds.Max[1], 1.0f);
    ASSERT_EQ(tri.Groups.size(), 1u);
    EXPECT_EQ(tri.Groups[0].Positions.Values, (std::vector<float>{ 0, 0, 0, 1, 0, 0, 1, 1, 0 }));
    ASSERT_TRUE(tri.Groups[0].Positions.File);
    EXPECT_EQ(tri.Groups[0].Positions.File->Ref.Path, "parts/positions.bin");
    ASSERT_TRUE(tri.Groups[0].BoneIndices);
    EXPECT_EQ(tri.Groups[0].BoneIndices->Values.size(), 12u);
    EXPECT_FALSE(tri.Groups[0].UV0);
    EXPECT_EQ(props.Meshes[1].Value.BoundsForm, SourceBoundsForm::None);

    ASSERT_EQ(props.Skeletons.size(), 1u);
    ASSERT_EQ(props.Skeletons[0].Value.Bones.size(), 2u);
    EXPECT_EQ(props.Skeletons[0].Value.Bones[1].Parent->Text, "Hips");
    EXPECT_EQ((*props.Skeletons[0].Value.Bones[1].Rest.Translation)[2], 0.12f);
    EXPECT_FALSE(props.Skins[0].Value.InvBindPoses);

    const SourceTrack &walk = props.SkeletalAnimations[0].Value.Tracks[0];
    EXPECT_EQ(walk.Target.Text, "Hips");
    EXPECT_EQ(walk.Property, CpkgAnimationProperty::Rotation);
    EXPECT_TRUE(walk.InterpolationPerKey);
    EXPECT_EQ(walk.Interpolations, (std::vector<AnimationInterpolation>{ AnimationInterpolation::Step, AnimationInterpolation::Cubic }));
    ASSERT_TRUE(walk.Tangents);
    EXPECT_TRUE(walk.Tangents->Values.empty());

    // About X by 90 degrees, then about Z by 90 degrees: X goes to Y, and Y to Z.
    ASSERT_TRUE(props.Skyboxes[0].Value.Orientation);
    const Vec4 &orientation = props.Skyboxes[0].Value.Orientation->Quaternion;
    for (float component : orientation)
        EXPECT_NEAR(component, 0.5f, 1e-6f);
    EXPECT_EQ(props.Skyboxes[0].Value.Intensity, 2.0f);

    ASSERT_EQ(props.Nodes.size(), 1u);
    const SourceNode &goblin = props.Nodes[0];
    EXPECT_EQ(goblin.Mobility, CpkgMobility::Movable);
    ASSERT_TRUE(goblin.Attachments.SkeletonInstance);
    EXPECT_EQ(goblin.Attachments.SkeletonInstance->Skeleton.Text, "Rig");
    EXPECT_EQ(goblin.Attachments.MeshInstances[0].Skin->Text, "Skin");
    ASSERT_EQ(goblin.Attachments.Children.size(), 3u);
    EXPECT_EQ(goblin.Attachments.Children[0].Bone->Text, "Spine");
    EXPECT_EQ(goblin.Attachments.Children[0].Attachments.Placements[0].Bundle.Text, "Weapons/Sword");
    EXPECT_EQ(goblin.Attachments.Children[1].Attachments.Camera->FovAngle, 1.2f);
    EXPECT_FALSE(goblin.Attachments.Children[1].Attachments.Camera->NearClip);
    const SourceLight &lamp = *goblin.Attachments.Children[2].Attachments.Light;
    EXPECT_EQ(lamp.Type, LightType::Spot);
    EXPECT_EQ(lamp.CastsShadows, true);
    EXPECT_FALSE(lamp.Enabled);
    EXPECT_EQ(lamp.ShadowResolution, 1024u);

    EXPECT_EQ(props.Placements[0].Mobility, CpkgMobility::Stationary);
    EXPECT_EQ(props.Placements[0].Transform.Rotation->Quaternion[3], 2.0f);
    EXPECT_EQ(props.MeshFields[0].Transforms.size(), 2u);
    EXPECT_EQ(props.ImportNodes[0].Export.Text, "Hand_R");
    EXPECT_EQ(props.Clips[0].Value.Duration, 2.0f);
    EXPECT_EQ(props.Clips[0].Value.SkeletalAnimations[0].SkeletonInstance.Text, "Goblin");
    ASSERT_TRUE(props.Exports);
    EXPECT_EQ(props.Exports->size(), 2u);
    ASSERT_TRUE(props.World);
    EXPECT_EQ(props.World->Regions[0].Bounds.Min[2], -10.0f);
    EXPECT_EQ(props.World->ActiveCamera->Text, "Eyes");
    EXPECT_EQ(props.World->BackgroundColor[1], 0.2f);

    ASSERT_EQ(props.Imports.size(), 1u);
    const SourceImport &import = props.Imports[0];
    EXPECT_EQ(import.File.Path, "art/goblin.fbx");
    EXPECT_EQ(import.Scale, 0.01f);
    EXPECT_FALSE(import.Nodes);
    EXPECT_EQ(import.Names[size_t(SourceImportKind::Meshes)]->Text, "G_{name}.mesh");
    ASSERT_EQ(import.Rename[size_t(SourceImportKind::Meshes)].size(), 1u);
    EXPECT_EQ(import.Rename[size_t(SourceImportKind::Meshes)][0].first.Text, "Cube.001");
    EXPECT_EQ(import.ReplaceMaterials[0].second.Text, "Shared/Skin");
    ASSERT_TRUE(import.Clips);
    ASSERT_EQ(import.Clips->size(), 2u);
    EXPECT_EQ((*import.Clips)[0].Frames, (std::array<uint32_t, 2>{ 0, 59 }));
    EXPECT_EQ((*import.Clips)[1].Stack->Text, "Take 001");
    ASSERT_EQ(import.Edits.size(), 1u);
    EXPECT_EQ(import.Edits[0].Name.Text, "Armature");
    EXPECT_FALSE(import.Edits[0].Value.Attachments.Light->Type);
    EXPECT_EQ(import.Edits[0].Value.Attachments.Children[0].Bone->Text, "Hand_R");

    // A map from names may hold format keys.
    const SourceBundle &village = package.Bundles[1];
    EXPECT_EQ(village.Name.Text, "Village");
    EXPECT_EQ(village.Where.File, 1u);
    ASSERT_EQ(village.Materials.size(), 1u);
    EXPECT_EQ(village.Materials[0].Name.Text, "Brass");
    EXPECT_EQ(village.Materials[0].Value.Metallic, 1.0f);
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgSource, ReportsShapeErrorsInDocumentOrder)
{
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "$note": "reserved",
  "bundles": [
    {
      "name": "Village",
      "colour": [1, 0, 0],
      "materials": {
        "$comment": 7,
        "Brass": { "roughness": "high", "baseColor": [1, 1, 1] }
      },
      "placements": [ { "bundle": "a//b" } ],
      "nodes": [ { "name": "Lamp/1", "camera": { "fovAngle": 1e39 } } ],
      "meshes": { "Crate": {} },
      "exports": ["$Hand"]
    }
  ]
}
)");

    SourcePackage package;
    const LoadResult result = Load("levels.cpkg.json", &package);
    EXPECT_FALSE(result.Loaded);
    EXPECT_EQ(result.Diagnostics,
        "levels.cpkg.json:4:3: error: unknown key \"$note\" [/$note]\n"
        "levels.cpkg.json:8:7: error: unknown key \"colour\" [/bundles/0/colour]\n"
        "levels.cpkg.json:10:21: error: expected a string [/bundles/0/materials/$comment]\n"
        "levels.cpkg.json:11:33: error: expected a number [/bundles/0/materials/Brass/roughness]\n"
        "levels.cpkg.json:11:54: error: expected an array of 4 numbers [/bundles/0/materials/Brass/baseColor]\n"
        "levels.cpkg.json:13:35: error: expected a bundle path: 1 or 2 names separated by '/' [/bundles/0/placements/0/bundle]\n"
        "levels.cpkg.json:14:28: error: expected a name: non-empty, not beginning with '$', with no '/' or NUL [/bundles/0/nodes/0/name]\n"
        "levels.cpkg.json:14:62: error: expected a number within the range of a 32-bit float [/bundles/0/nodes/0/camera/fovAngle]\n"
        "levels.cpkg.json:15:28: error: missing required key \"groups\" [/bundles/0/meshes/Crate]\n"
        "levels.cpkg.json:16:19: error: expected a name: non-empty, not beginning with '$', with no '/' or NUL [/bundles/0/exports/0]\n");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgSource, ReadsNumbersAtTheEdgesOfTheirRange)
{
    // 3.4028235e38 is FLT_MAX's shortest spelling, though as a double it exceeds FLT_MAX.
    WriteText("edges.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Edges",
  "bundles": [ { "name": "B", "materials": { "M": { "roughness": 3.4028235e38, "metallic": -3.4028235e38, "ambientOcclusion": 1e-50 } },
                 "nodes": [ { "light": { "type": "point", "shadowResolution": 4294967295 } },
                            { "light": { "type": "point", "shadowResolution": 4294967296 } },
                            { "light": { "type": "point", "shadowResolution": 2.0 } },
                            { "light": { "type": "point", "shadowResolution": 2.5 } } ] } ]
}
)");

    SourcePackage package;
    const LoadResult result = Load("edges.cpkg.json", &package);
    EXPECT_EQ(result.Diagnostics,
        "edges.cpkg.json:6:79: error: expected an integer from 0 to 4294967295 [/bundles/0/nodes/1/light/shadowResolution]\n"
        "edges.cpkg.json:8:79: error: expected an integer [/bundles/0/nodes/3/light/shadowResolution]\n");

    const SourceMaterial &material = package.Bundles[0].Materials[0].Value;
    EXPECT_EQ(material.Roughness, FLT_MAX);
    EXPECT_EQ(material.Metallic, -FLT_MAX);
    EXPECT_EQ(material.AmbientOcclusion, 0.0f);
    EXPECT_EQ(package.Bundles[0].Nodes[0].Attachments.Light->ShadowResolution, UINT32_MAX);
    EXPECT_EQ(package.Bundles[0].Nodes[2].Attachments.Light->ShadowResolution, 2u);
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgSource, ReportsBundleFileErrors)
{
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    { "include": "missing.cbundle.json" },
    { "include": "village.cbundle.json" },
    { "include": "./village.cbundle.json" },
    { "include": "other.json" },
    { "include": "broken.cbundle.json" },
    { "include": "wrong.cbundle.json" }
  ]
}
)");
    WriteText("village.cbundle.json", R"({ "formatVersion": 1, "bundle": { "name": "Village" } })");
    WriteText("other.json", R"({ "formatVersion": 1, "bundle": { "name": "Other" } })");
    WriteText("broken.cbundle.json", "{\n  \"formatVersion\": 1,\n  \"bundle\": { \"name\": \"B\", }\n}\n");
    WriteText("wrong.cbundle.json", R"({ "formatVersion": 2, "packageName": "P", "bundle": { "name": "W" } })");

    SourcePackage package;
    const LoadResult result = Load("levels.cpkg.json", &package);
    EXPECT_FALSE(result.Loaded);

    std::istringstream lines(result.Diagnostics);
    std::vector<std::string> diagnostics;
    for (std::string line; std::getline(lines, line);)
        diagnostics.push_back(line);
    ASSERT_EQ(diagnostics.size(), 6u) << result.Diagnostics;
    EXPECT_EQ(diagnostics[0], "levels.cpkg.json:5:18: error: file not found: \"missing.cbundle.json\" [/bundles/0/include]");
    EXPECT_EQ(diagnostics[1], "levels.cpkg.json:7:18: error: the bundle file \"./village.cbundle.json\" is already included [/bundles/2/include]");
    EXPECT_EQ(diagnostics[2], "levels.cpkg.json:8:18: error: expected a bundle file name ending in \".cbundle.json\" [/bundles/3/include]");
    EXPECT_EQ(diagnostics[3].rfind("broken.cbundle.json:3:", 0), 0u) << diagnostics[3];
    EXPECT_NE(diagnostics[3].find(": error: "), std::string::npos) << diagnostics[3];
    EXPECT_EQ(diagnostics[3].find('['), std::string::npos) << diagnostics[3];
    EXPECT_EQ(diagnostics[4], "wrong.cbundle.json:1:20: error: expected format version 1 [/formatVersion]");
    EXPECT_EQ(diagnostics[5], "wrong.cbundle.json:1:23: error: unknown key \"packageName\" [/packageName]");

    ASSERT_EQ(package.Bundles.size(), 6u);
    EXPECT_EQ(package.Bundles[1].Name.Text, "Village");
    EXPECT_EQ(package.Bundles[5].Name.Text, "W");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgSource, ReportsArrayErrors)
{
    WriteBinary<float>("pos.bin", { 0, 0, 0, 1, 0, 0 });
    WriteText("bad.bin", "0123456789");
    WriteText("arrays.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Arrays",
  "bundles": [ { "name": "Mesh", "meshes": { "Quad": { "groups": [ {
    "positions": { "file": "pos.bin" },
    "normals": { "file": "bad.bin" },
    "uv0": [0, 0, 1],
    "tangents": { "file": "none.bin" },
    "boneIndices": [1, -1, 2, 3]
  } ] } } } ]
}
)");

    SourcePackage package;
    const LoadResult result = Load("arrays.cpkg.json", &package);
    EXPECT_FALSE(result.Loaded);
    EXPECT_EQ(result.Diagnostics,
        "arrays.cpkg.json:6:26: error: the size of \"bad.bin\", 10 bytes, is not a multiple of its element size, 12 bytes [/bundles/0/meshes/Quad/groups/0/normals/file]\n"
        "arrays.cpkg.json:7:12: error: expected a multiple of 2 numbers; found 3 [/bundles/0/meshes/Quad/groups/0/uv0]\n"
        "arrays.cpkg.json:8:27: error: file not found: \"none.bin\" [/bundles/0/meshes/Quad/groups/0/tangents/file]\n"
        "arrays.cpkg.json:9:24: error: expected an integer from 0 to 4294967295 [/bundles/0/meshes/Quad/groups/0/boneIndices/1]\n");

    const SourceMeshGroup &group = package.Bundles[0].Meshes[0].Value.Groups[0];
    EXPECT_EQ(group.Positions.Values, (std::vector<float>{ 0, 0, 0, 1, 0, 0 }));
    EXPECT_FALSE(group.Normals);
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgSource, ReadsArraysFromRangesOfAFile)
{
    // 96 bytes: three positions, three normals, then three UVs.
    WriteBinary<float>("tri.bin", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 23, 24, 25 });
    WriteText("good.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Good",
  "bundles": [ { "name": "Mesh", "meshes": { "Tri": { "groups": [ {
    "positions": { "file": "tri.bin", "count": 3 },
    "normals": { "file": "tri.bin", "offset": 36, "count": 3 },
    "uv0": { "file": "tri.bin", "offset": 72 }
  } ] } } } ]
}
)");

    SourcePackage package;
    const LoadResult good = Load("good.cpkg.json", &package);
    EXPECT_EQ(good.Diagnostics, "");
    ASSERT_TRUE(good.Loaded);
    const SourceMeshGroup &group = package.Bundles[0].Meshes[0].Value.Groups[0];
    EXPECT_EQ(group.Positions.Values, (std::vector<float>{ 0, 1, 2, 3, 4, 5, 6, 7, 8 }));
    EXPECT_FALSE(group.Positions.File->Offset);
    EXPECT_EQ(group.Positions.File->Count, 3u);
    ASSERT_TRUE(group.Normals);
    EXPECT_EQ(group.Normals->Values, (std::vector<float>{ 10, 11, 12, 13, 14, 15, 16, 17, 18 }));
    EXPECT_EQ(group.Normals->File->Offset, 36u);
    ASSERT_TRUE(group.UV0);
    EXPECT_EQ(group.UV0->Values, (std::vector<float>{ 20, 21, 22, 23, 24, 25 }));
    EXPECT_FALSE(group.UV0->File->Count);

    WriteText("bad.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Bad",
  "bundles": [ { "name": "Mesh", "meshes": { "Tri": { "groups": [ {
    "positions": { "file": "tri.bin", "offset": 2 },
    "normals": { "file": "tri.bin", "offset": 100 },
    "uv0": { "file": "tri.bin", "count": 13 },
    "tangents": { "file": "tri.bin", "offset": 4 },
    "boneWeights": { "file": "tri.bin", "count": -1 }
  } ] } } } ]
}
)");

    SourcePackage badPackage;
    const LoadResult bad = Load("bad.cpkg.json", &badPackage);
    EXPECT_FALSE(bad.Loaded);
    EXPECT_EQ(bad.Diagnostics,
        "bad.cpkg.json:5:49: error: expected a byte offset: a non-negative integer that is a multiple of 4 [/bundles/0/meshes/Tri/groups/0/positions/offset]\n"
        "bad.cpkg.json:6:47: error: offset 100 is past the end of \"tri.bin\", 96 bytes [/bundles/0/meshes/Tri/groups/0/normals/offset]\n"
        "bad.cpkg.json:7:42: error: 13 elements of 8 bytes from offset 0 extend past the end of \"tri.bin\", 96 bytes [/bundles/0/meshes/Tri/groups/0/uv0/count]\n"
        "bad.cpkg.json:8:27: error: the 92 bytes of \"tri.bin\" from offset 4 are not a multiple of its element size, 16 bytes [/bundles/0/meshes/Tri/groups/0/tangents/file]\n"
        "bad.cpkg.json:9:50: error: expected an integer from 0 to 4294967295 [/bundles/0/meshes/Tri/groups/0/boneWeights/count]\n");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgSource, ReportsFilesItCannotRead)
{
    SourcePackage package;
    LoadResult result = Load("missing.cpkg.json", &package);
    EXPECT_FALSE(result.Loaded);
    EXPECT_EQ(result.Diagnostics, "missing.cpkg.json: error: file not found\n");

    WriteText("text.cpkg.json", "[1, 2]");
    result = Load("text.cpkg.json", &package);
    EXPECT_EQ(result.Diagnostics, "text.cpkg.json:1:1: error: expected an object []\n");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgSource, ValidateCommand)
{
    WriteText("good.cpkg.json", R"({ "formatVersion": 1, "packageName": "Good", "bundles": [ { "name": "B" } ] })");
    WriteText("bad.cpkg.json", R"({ "formatVersion": 1, "packageName": "Bad", "bundles": [ { "name": "" } ] })");

    std::string output;
    EXPECT_EQ(RunValidate({ "good.cpkg.json" }, &output), kExitSuccess);
    EXPECT_EQ(output, "");

    EXPECT_EQ(RunValidate({ "bad.cpkg.json" }, &output), kExitError);
    EXPECT_EQ(output, "bad.cpkg.json:1:68: error: expected a name: non-empty, not beginning with '$', with no '/' or NUL [/bundles/0/name]\n");

    EXPECT_EQ(RunValidate({}, &output), kExitUsage);
    EXPECT_EQ(output, "cpkg: error: validate: missing argument <package.cpkg.json>\nRun 'cpkg --help' for usage.\n");

    EXPECT_EQ(RunValidate({ "good.cpkg" }, &output), kExitUsage);
    EXPECT_EQ(output, "cpkg: error: validate: expected a .cpkg.json file: good.cpkg\nRun 'cpkg --help' for usage.\n");
}
