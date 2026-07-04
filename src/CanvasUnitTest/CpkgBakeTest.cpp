#include "pch.h"

#include "BakeManifest.h"
#include "BakePaths.h"
#include "CpkgBlobTypes.h"
#include "CpkgIO.h"
#include "CpkgSource.h"
#include "CpkgTestUtil.h"
#include "SerializeScene.h"
#include "TextureTable.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace CanvasUnitTest
{

using namespace Canvas;
using namespace Canvas::Cpkg;
namespace fs = std::filesystem;

namespace
{
    // Write UTF-8 text to a path (used to lay down temporary manifest / texture files).
    void WriteTextFile(const fs::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // Build ResolvedPaths directly from UTF-8 anchor directories, bypassing manifest loading.
    ResolvedPaths MakeResolvedPaths(const char* manifestDir, const char* outputDir)
    {
        ResolvedPaths paths;
        paths.manifestDir   = fs::u8path(manifestDir);
        paths.outputDir     = fs::u8path(outputDir);
        paths.outputAbsPath = paths.outputDir / "out.cpkg";
        return paths;
    }
}

//==================================================================================================
// LoadBakeManifest
//==================================================================================================

TEST(CpkgBakeTest, LoadManifestRelativePaths)
{
    TempFile tmp("cpkg_manifest_rel.cpkg.json");
    WriteTextFile(tmp.path, R"({
        "name": "ForestScene",
        "output": "../build/ForestScene.cpkg",
        "sources": [
            {
                "type": "fbx",
                "path": "scene/ForestScene.fbx",
                "textureSearchPaths": [ "textures/materials", "../../shared/textures" ]
            }
        ],
        "textures": [
            { "name": "sky_px", "path": "textures/sky/px.hdr", "embed": true }
        ],
        "options": { "defaultEmbed": false }
    })");

    BakeManifest manifest;
    ASSERT_EQ(LoadBakeManifest(tmp.str().c_str(), &manifest), Gem::Result::Success);

    EXPECT_EQ(manifest.name, "ForestScene");
    EXPECT_EQ(manifest.outputPath, "../build/ForestScene.cpkg");
    EXPECT_FALSE(manifest.defaultEmbed);

    ASSERT_EQ(manifest.sources.size(), 1u);
    EXPECT_EQ(manifest.sources[0].type, "fbx");
    EXPECT_EQ(manifest.sources[0].path, "scene/ForestScene.fbx");
    ASSERT_EQ(manifest.sources[0].textureSearchPaths.size(), 2u);
    EXPECT_EQ(manifest.sources[0].textureSearchPaths[0], "textures/materials");
    EXPECT_EQ(manifest.sources[0].textureSearchPaths[1], "../../shared/textures");

    ASSERT_EQ(manifest.textures.size(), 1u);
    EXPECT_EQ(manifest.textures[0].name, "sky_px");
    EXPECT_EQ(manifest.textures[0].path, "textures/sky/px.hdr");
    EXPECT_TRUE(manifest.textures[0].embed);
}

TEST(CpkgBakeTest, LoadManifestAbsolutePathsAcceptedAsIs)
{
    TempFile tmp("cpkg_manifest_abs.cpkg.json");
    WriteTextFile(tmp.path, R"({
        "output": "C:/build/out.cpkg",
        "sources": [ { "type": "fbx", "path": "C:/content/scene.fbx" } ],
        "textures": [ { "name": "rock", "path": "C:/content/textures/rock.png" } ]
    })");

    BakeManifest manifest;
    ASSERT_EQ(LoadBakeManifest(tmp.str().c_str(), &manifest), Gem::Result::Success);

    EXPECT_EQ(manifest.outputPath, "C:/build/out.cpkg");
    ASSERT_EQ(manifest.sources.size(), 1u);
    EXPECT_EQ(manifest.sources[0].path, "C:/content/scene.fbx");
    ASSERT_EQ(manifest.textures.size(), 1u);
    EXPECT_EQ(manifest.textures[0].path, "C:/content/textures/rock.png");
    // embed omitted -> falls back to defaultEmbed (false by default)
    EXPECT_FALSE(manifest.textures[0].embed);
}

TEST(CpkgBakeTest, LoadManifestMissingOutputFails)
{
    TempFile tmp("cpkg_manifest_no_output.cpkg.json");
    WriteTextFile(tmp.path, R"({ "name": "NoOutput", "sources": [] })");

    BakeManifest manifest;
    EXPECT_EQ(LoadBakeManifest(tmp.str().c_str(), &manifest), Gem::Result::InvalidArg);
}

TEST(CpkgBakeTest, LoadManifestNullArgs)
{
    BakeManifest manifest;
    EXPECT_EQ(LoadBakeManifest(nullptr, &manifest), Gem::Result::BadPointer);
    EXPECT_EQ(LoadBakeManifest("whatever.json", nullptr), Gem::Result::BadPointer);
}

//==================================================================================================
// ResolveBakePaths / MakePkgRelative
//==================================================================================================

TEST(CpkgBakeTest, ResolveBakePathsRelativeOutput)
{
    BakeManifest manifest;
    manifest.outputPath = "../build/out.cpkg";

    ResolvedPaths resolved;
    ASSERT_EQ(ResolveBakePaths(manifest, "C:/proj/content/scene.cpkg.json", &resolved),
              Gem::Result::Success);

    EXPECT_EQ(PathToString(resolved.outputAbsPath), "C:/proj/build/out.cpkg");
    EXPECT_EQ(PathToString(resolved.outputDir), "C:/proj/build");
    EXPECT_EQ(PathToString(resolved.manifestDir), "C:/proj/content");
}

TEST(CpkgBakeTest, ResolveBakePathsMissingOutputFails)
{
    BakeManifest manifest; // outputPath empty
    ResolvedPaths resolved;
    EXPECT_EQ(ResolveBakePaths(manifest, "C:/proj/content/scene.cpkg.json", &resolved),
              Gem::Result::InvalidArg);
}

TEST(CpkgBakeTest, MakePkgRelativeExpressesRelativeToOutputDir)
{
    ResolvedPaths paths = MakeResolvedPaths("C:/proj/content", "C:/proj/build");
    EXPECT_EQ(MakePkgRelative(paths, "C:/proj/content/textures/foo.png"),
              "../content/textures/foo.png");
}

//==================================================================================================
// TextureTable
//==================================================================================================

TEST(CpkgBakeTest, TextureTableDeduplicatesByAbsolutePath)
{
    ResolvedPaths paths = MakeResolvedPaths("C:/proj/content", "C:/proj/build");

    TextureTable table;
    ManifestTexture declared;
    declared.name = "rock";
    declared.path = "textures/rock.png"; // resolves to C:/proj/content/textures/rock.png
    const int32_t manifestIndex = table.AddManifestTexture(declared, paths);

    // The same texture arrives again from an FBX material via its resolved absolute path.
    const int32_t fbxIndex = table.AddFbxTexture("C:/proj/content/textures/rock.png", paths);

    EXPECT_EQ(manifestIndex, fbxIndex);

    const std::vector<PackageTexture> textures = table.Finalize();
    ASSERT_EQ(textures.size(), 1u);
    EXPECT_EQ(textures[0].Name, "rock"); // manifest entry (added first) keeps its name
    EXPECT_EQ(textures[0].Path, "../content/textures/rock.png");
}

TEST(CpkgBakeTest, TextureTableFbxSearchPathFallback)
{
    // Lay down a real texture in a temp directory that the "broken" FBX path does not point to.
    const fs::path dir = fs::temp_directory_path() / "cpkg_texsearch";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path rock = dir / "rock.png";
    WriteTextFile(rock, "not really a png");

    const std::string resolved =
        TextureTable::ResolveFbxTexturePath("C:/old-machine/textures/rock.png", { PathToString(dir) });

    EXPECT_EQ(fs::u8path(resolved).lexically_normal(), rock.lexically_normal());

    fs::remove_all(dir, ec);
}

TEST(CpkgBakeTest, TextureTableFbxSearchPathUnresolved)
{
    const std::string resolved =
        TextureTable::ResolveFbxTexturePath("C:/nowhere/missing.png", { "C:/also-nowhere" });
    EXPECT_TRUE(resolved.empty());
}

//==================================================================================================
// SerializeScene
//==================================================================================================

TEST(CpkgBakeTest, SerializeSceneRemapsMaterialTextureIndices)
{
    ResolvedPaths paths = MakeResolvedPaths("C:/content", "C:/out");

    // Pre-populate the table in the REVERSE order of the scene's texture list, so a passthrough
    // (identity) mapping would fail the assertions and only a real remap succeeds.
    TextureTable table;
    const int32_t idxB = table.AddFbxTexture("C:/content/tex/b.png", paths); // table index 0
    const int32_t idxA = table.AddFbxTexture("C:/content/tex/a.png", paths); // table index 1

    Canvas::Fbx::ImportedScene scene;

    Canvas::Fbx::ImportedTextureRef refA;
    refA.AbsoluteFilePath = "C:/content/tex/a.png"; // scene texture 0
    Canvas::Fbx::ImportedTextureRef refB;
    refB.AbsoluteFilePath = "C:/content/tex/b.png"; // scene texture 1
    scene.Textures = { refA, refB };

    Canvas::Fbx::ImportedMaterial mat0;
    mat0.Name = "uses_a";
    mat0.AlbedoTextureIndex = 0; // -> a.png
    Canvas::Fbx::ImportedMaterial mat1;
    mat1.Name = "uses_b";
    mat1.AlbedoTextureIndex = 1; // -> b.png
    scene.Materials = { mat0, mat1 };

    PackageData out;
    SerializeScene(scene, table, &out);

    ASSERT_EQ(out.Materials.size(), 2u);
    EXPECT_EQ(out.Materials[0].AlbedoTextureIndex, idxA); // a.png -> table index 1
    EXPECT_EQ(out.Materials[1].AlbedoTextureIndex, idxB); // b.png -> table index 0

    // The finalized texture list comes from the table, not the raw scene list.
    ASSERT_EQ(out.Textures.size(), 2u);
}

TEST(CpkgBakeTest, SerializeSceneCopiesCoreFields)
{
    TextureTable table; // empty; no textures referenced

    Canvas::Fbx::ImportedScene scene;

    Canvas::Fbx::ImportedNode node;
    node.Name = "root";
    node.Translation = { 1.0f, 2.0f, 3.0f, 0.0f };
    node.MeshIndex = 0;
    scene.Nodes = { node };

    Canvas::Fbx::ImportedCamera cam;
    cam.Name = "cam";
    cam.NearClip = 0.5f;
    cam.FarClip  = 250.0f;
    cam.FovAngle = 1.1f;
    cam.AspectRatio = 1.5f;
    scene.Cameras = { cam };
    scene.ActiveCameraNodeIndex = 0;

    PackageData out;
    SerializeScene(scene, table, &out);

    ASSERT_EQ(out.Nodes.size(), 1u);
    EXPECT_EQ(out.Nodes[0].Name, "root");
    EXPECT_EQ(out.Nodes[0].Translation.V[0], 1.0f);
    EXPECT_EQ(out.Nodes[0].Translation.V[2], 3.0f);
    EXPECT_EQ(out.Nodes[0].MeshIndex, 0);

    ASSERT_EQ(out.Cameras.size(), 1u);
    EXPECT_EQ(out.Cameras[0].NearZ, 0.5f);
    EXPECT_EQ(out.Cameras[0].FarZ, 250.0f);
    EXPECT_EQ(out.Cameras[0].FovY, 1.1f);
    EXPECT_EQ(out.Cameras[0].AspectRatio, 1.5f);
    EXPECT_EQ(out.ActiveCameraNodeIndex, 0);
}

//==================================================================================================
// WritePackage
//==================================================================================================

namespace
{
    // A small but multi-chunk scene: NODE + MESH + MATL + TXTR + LITE + CAMR (no ANIM).
    PackageData MakeSampleScene()
    {
        PackageData data;

        PackageNode root;
        root.Name = "root";
        root.MeshIndex = 0;
        PackageNode camNode;
        camNode.Name = "camNode";
        camNode.ParentIndex = 0;
        camNode.CameraIndex = 0;
        data.Nodes = { root, camNode };
        data.ActiveCameraNodeIndex = 1;

        PackageMesh mesh;
        mesh.Name = "tri";
        PackageMeshPart part;
        part.MaterialIndex = 0;
        for (int i = 0; i < 3; ++i)
        {
            const float f = static_cast<float>(i);
            part.Positions.push_back({ { f, f + 1.0f, f + 2.0f, 1.0f } });
            part.Normals.push_back({ { 0.0f, 0.0f, 1.0f, 0.0f } });
        }
        mesh.Parts.push_back(part);
        data.Meshes = { mesh };

        PackageMaterial mat;
        mat.Name = "mat";
        data.Materials = { mat };

        PackageTexture tex; // external (path-only) texture
        tex.Name = "rock";
        tex.Path = "../content/textures/rock.png";
        data.Textures = { tex };

        PackageLight light;
        light.Name = "sun";
        light.Type = LightType::Directional;
        data.Lights = { light };

        PackageCamera cam;
        cam.Name = "cam";
        data.Cameras = { cam };

        return data;
    }
}

TEST(CpkgBakeTest, WritePackageProducesValidContainer)
{
    TempFile tmp("cpkg_writepackage.cpkg");
    const PackageData data = MakeSampleScene();

    ASSERT_EQ(data.WritePackage(tmp.str().c_str()), Gem::Result::Success);

    CCpkgSource source;
    ASSERT_EQ(CCpkgSource::OpenFile(tmp.str().c_str(), &source), Gem::Result::Success);

    // ReadCpkgHeader validates magic, the little-endian flag, and the header CRC32.
    CpkgHeaderData header;
    ASSERT_EQ(ReadCpkgHeader(source, &header), Gem::Result::Success);
    EXPECT_EQ(header.Magic, CPKG_MAGIC);
    EXPECT_EQ(header.ChunkCount, 6u); // NODE, MESH, MATL, TXTR, LITE, CAMR

    std::vector<ChunkEntryData> entries;
    ASSERT_EQ(ReadChunkTable(source, header.ChunkCount, &entries), Gem::Result::Success);
    ASSERT_EQ(entries.size(), 6u);

    std::vector<uint32_t> fourccs;
    for (const ChunkEntryData& e : entries)
    {
        fourccs.push_back(e.FourCC);
        // Every chunk lands after the header + table and within the file.
        EXPECT_GE(e.Offset, CPKG_HEADER_SIZE);
        EXPECT_LE(e.Offset + e.SizeRaw, source.Size());
        EXPECT_EQ(e.SizeCompressed, e.SizeRaw); // v1 is uncompressed
    }

    auto has = [&](uint32_t fourcc) {
        return std::find(fourccs.begin(), fourccs.end(), fourcc) != fourccs.end();
    };
    EXPECT_TRUE(has(CPKG_FOURCC_NODE));
    EXPECT_TRUE(has(CPKG_FOURCC_MESH));
    EXPECT_TRUE(has(CPKG_FOURCC_MATL));
    EXPECT_TRUE(has(CPKG_FOURCC_TXTR));
    EXPECT_TRUE(has(CPKG_FOURCC_LITE));
    EXPECT_TRUE(has(CPKG_FOURCC_CAMR));
}

TEST(CpkgBakeTest, WritePackageEmptySceneWritesHeaderOnly)
{
    TempFile tmp("cpkg_writepackage_empty.cpkg");
    PackageData data; // nothing populated

    ASSERT_EQ(data.WritePackage(tmp.str().c_str()), Gem::Result::Success);

    CCpkgSource source;
    ASSERT_EQ(CCpkgSource::OpenFile(tmp.str().c_str(), &source), Gem::Result::Success);

    CpkgHeaderData header;
    ASSERT_EQ(ReadCpkgHeader(source, &header), Gem::Result::Success);
    EXPECT_EQ(header.ChunkCount, 0u);
}

TEST(CpkgBakeTest, WritePackageNullPathFails)
{
    PackageData data;
    EXPECT_EQ(data.WritePackage(nullptr), Gem::Result::BadPointer);
}

} // namespace CanvasUnitTest
