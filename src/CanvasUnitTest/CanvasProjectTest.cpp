//================================================================================================
// CanvasProjectTest - Tests for CProjectManifest
//================================================================================================

#include "pch.h"
#include "CanvasProject.h"
#include "SchemaCheck.h"

#include <filesystem>
#include <fstream>

namespace
{
    // The example manifest from the CanvasProject spec.
    constexpr std::string_view kExample = R"({
  "formatVersion": 1,
  "name": "Game",
  "output": "build",
  "packages": [
    { "name": "Levels",         "source":  "levels/levels.cpkg.json" },
    { "name": "Characters",     "source":  "../Shared/characters/characters.cpkg.json" },
    { "name": "Textures",       "source":  "D:/Assets/library/library.cpkg.json" },
    { "name": "VendorTextures", "package": "vendor/textures.cpkg" },
    { "name": "VendorProps",    "package": "vendor/props.cpkg",
      "references": { "Textures": "VendorTextures" } }
  ],
  "worlds": [ "Levels/MainMenu", "Levels/Level1", "Levels/Level2" ],
  "defaultWorld": "Levels/MainMenu"
}
)";

    constexpr const char *kExamplePath = "D:/Proj/game.cproj.json";

    // The built project the CanvasProject spec shows for the example.
    constexpr std::string_view kExampleBuilt = R"({
  "formatVersion": 1,
  "name": "Game",
  "packages": [
    { "name": "Levels", "package": "levels.cpkg" },
    { "name": "Characters", "package": "characters.cpkg" },
    { "name": "Textures", "package": "library.cpkg" },
    { "name": "VendorTextures", "package": "textures.cpkg" },
    {
      "name": "VendorProps",
      "package": "props.cpkg",
      "references": { "Textures": "VendorTextures" }
    }
  ],
  "worlds": ["Levels/MainMenu", "Levels/Level1", "Levels/Level2"],
  "defaultWorld": "Levels/MainMenu"
}
)";

    // A valid manifest with the given members added after "packages".
    std::string Manifest(std::string_view packages, std::string_view more = {})
    {
        std::string text = "{\n  \"formatVersion\": 1,\n  \"name\": \"P\",\n  \"packages\": [";
        text += packages;
        text += "]";
        if (!more.empty())
        {
            text += ",\n  ";
            text += more;
        }
        text += "\n}\n";
        return text;
    }

    std::vector<ProjectError> ParseErrors(std::string_view text)
    {
        CProjectManifest manifest;
        std::vector<ProjectError> errors;
        EXPECT_EQ(manifest.Parse(text, "p.cproj.json", &errors), Result::CorruptedData);
        EXPECT_TRUE(manifest.GetPackages().empty());
        EXPECT_FALSE(errors.empty());
        for (const ProjectError &error : errors)
        {
            EXPECT_FALSE(error.Message.empty());
            EXPECT_GE(error.Line, 1u);
            EXPECT_GE(error.Column, 1u);
        }
        return errors;
    }

    // Expects exactly one error, at pointer, and returns it.
    ProjectError OneError(std::string_view text, std::string_view pointer)
    {
        const std::vector<ProjectError> errors = ParseErrors(text);
        EXPECT_EQ(errors.size(), 1u);
        if (errors.empty())
            return {};
        EXPECT_EQ(errors[0].Pointer, pointer) << errors[0].Message;
        return errors[0];
    }

    // As OneError, for a rule of the manifest's shape, which the schema also rejects.
    ProjectError OneShapeError(std::string_view text, std::string_view pointer)
    {
        EXPECT_TRUE(TextBreaksSchema(CanvasSchema::Project, text));
        return OneError(text, pointer);
    }

    class CanvasProjectFiles : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const ::testing::TestInfo *pInfo = ::testing::UnitTest::GetInstance()->current_test_info();
            m_Dir = std::filesystem::temp_directory_path() / (std::string("CanvasProject_") + pInfo->name());
            std::filesystem::remove_all(m_Dir);
            std::filesystem::create_directories(m_Dir);
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(m_Dir, error);
        }

        std::filesystem::path m_Dir;
    };

    std::string ToUtf8(const std::filesystem::path &path)
    {
        const std::u8string utf8 = path.u8string();
        return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
    }
}

TEST(CanvasProject, ReadsExample)
{
    CProjectManifest manifest;
    std::vector<ProjectError> errors;
    EXPECT_TRUE(TextMatchesSchema(CanvasSchema::Project, kExample));
    ASSERT_EQ(manifest.Parse(kExample, kExamplePath, &errors), Result::Success);
    EXPECT_TRUE(errors.empty());

    EXPECT_EQ(manifest.GetPath(), kExamplePath);
    EXPECT_EQ(manifest.GetName(), "Game");
    EXPECT_EQ(manifest.GetOutput(), "D:/Proj/build");

    const std::span<const ProjectPackage> packages = manifest.GetPackages();
    ASSERT_EQ(packages.size(), 5u);
    EXPECT_EQ(packages[0].Name, "Levels");
    EXPECT_EQ(packages[0].Source, "levels/levels.cpkg.json");
    EXPECT_TRUE(packages[0].Package.empty());
    EXPECT_EQ(packages[0].FilePath, "D:/Proj/levels/levels.cpkg.json");
    EXPECT_EQ(packages[1].FilePath, "D:/Shared/characters/characters.cpkg.json");
    EXPECT_EQ(packages[2].FilePath, "D:/Assets/library/library.cpkg.json");
    EXPECT_TRUE(packages[3].Source.empty());
    EXPECT_EQ(packages[3].Package, "vendor/textures.cpkg");
    EXPECT_EQ(packages[3].FilePath, "D:/Proj/vendor/textures.cpkg");
    ASSERT_EQ(packages[4].References.size(), 1u);
    EXPECT_EQ(packages[4].References[0].Name, "Textures");
    EXPECT_EQ(packages[4].References[0].ProjectName, "VendorTextures");

    const std::span<const ProjectWorld> worlds = manifest.GetWorlds();
    ASSERT_EQ(worlds.size(), 3u);
    EXPECT_EQ(worlds[0].Package, 0u);
    EXPECT_EQ(worlds[0].Bundle, "MainMenu");
    EXPECT_EQ(worlds[2].Bundle, "Level2");
    EXPECT_EQ(manifest.GetDefaultWorld(), 0u);

    size_t index = 99;
    EXPECT_EQ(manifest.FindPackage("VendorTextures", &index), Result::Success);
    EXPECT_EQ(index, 3u);
    EXPECT_EQ(manifest.FindPackage("vendortextures", &index), Result::NotFound);
}

TEST(CanvasProject, OptionalMembers)
{
    CProjectManifest manifest;
    ASSERT_EQ(manifest.Parse(Manifest(R"({ "name": "A", "package": "a.cpkg" })"), "dir/p.cproj.json", nullptr), Result::Success);
    EXPECT_EQ(manifest.GetOutput(), "dir/build");
    EXPECT_EQ(manifest.GetPackages()[0].FilePath, "dir/a.cpkg");
    EXPECT_TRUE(manifest.GetWorlds().empty());
    EXPECT_EQ(manifest.GetDefaultWorld(), kNoWorld);

    // Backslashes in the manifest's own path are separators.
    ASSERT_EQ(manifest.Parse(Manifest(R"({ "name": "A", "package": "../a.cpkg" })"), "D:\\Proj\\Sub\\p.cproj.json", nullptr), Result::Success);
    EXPECT_EQ(manifest.GetPackages()[0].FilePath, "D:/Proj/a.cpkg");
    EXPECT_EQ(manifest.GetPath(), "D:\\Proj\\Sub\\p.cproj.json");

    EXPECT_TRUE(TextMatchesSchema(CanvasSchema::Project, Manifest(R"({ "name": "A", "package": "a.cpkg" })")));

    // An empty package list and a "$schema" are fine.
    EXPECT_TRUE(TextMatchesSchema(CanvasSchema::Project, R"({ "$schema": "project.schema.json", "formatVersion": 1, "name": "E", "packages": [] })"));
    ASSERT_EQ(manifest.Parse(R"({ "$schema": "project.schema.json", "formatVersion": 1, "name": "E", "packages": [] })", "p.cproj.json", nullptr), Result::Success);
    EXPECT_TRUE(manifest.GetPackages().empty());
}

TEST(CanvasProject, ResolvesPackageNames)
{
    CProjectManifest manifest;
    ASSERT_EQ(manifest.Parse(kExample, kExamplePath, nullptr), Result::Success);

    size_t index = 99;

    // Rule 1: the package's own declared name, even when a project name or reference matches.
    EXPECT_EQ(manifest.ResolvePackageName(3, "Textures", "Textures", &index), Result::Success);
    EXPECT_EQ(index, 3u);

    // Rule 2: a key of the package's references.
    EXPECT_EQ(manifest.ResolvePackageName(4, "Props", "Textures", &index), Result::Success);
    EXPECT_EQ(index, 3u);

    // Rule 3: any other name is a project name.
    EXPECT_EQ(manifest.ResolvePackageName(0, "Levels", "Textures", &index), Result::Success);
    EXPECT_EQ(index, 2u);
    EXPECT_EQ(manifest.ResolvePackageName(4, "Props", "Levels", &index), Result::Success);
    EXPECT_EQ(index, 0u);

    index = 99;
    EXPECT_EQ(manifest.ResolvePackageName(0, "Levels", "Missing", &index), Result::NotFound);
    EXPECT_EQ(manifest.ResolvePackageName(5, "Levels", "Levels", &index), Result::InvalidArg);
    EXPECT_EQ(index, 99u);
}

TEST(CanvasProject, BuiltProject)
{
    CProjectManifest manifest;
    ASSERT_EQ(manifest.Parse(kExample, kExamplePath, nullptr), Result::Success);

    EXPECT_EQ(manifest.GetBuiltFileName(0), "levels.cpkg");
    EXPECT_EQ(manifest.GetBuiltFileName(2), "library.cpkg");
    EXPECT_EQ(manifest.GetBuiltFileName(4), "props.cpkg");
    EXPECT_EQ(manifest.GetBuiltFileName(5), "");

    std::string text;
    ASSERT_EQ(manifest.FormatBuiltProject(&text, nullptr), Result::Success);
    EXPECT_EQ(text, kExampleBuilt);
    EXPECT_TRUE(TextMatchesSchema(CanvasSchema::Project, text));

    // The built project reads back as a project whose every entry is a package.
    CProjectManifest built;
    ASSERT_EQ(built.Parse(text, "D:/Proj/build/game.cproj.json", nullptr), Result::Success);
    for (const ProjectPackage &package : built.GetPackages())
        EXPECT_TRUE(package.Source.empty());
    EXPECT_EQ(built.GetPackages()[4].FilePath, "D:/Proj/build/props.cpkg");
    EXPECT_EQ(built.GetDefaultWorld(), 0u);
}

TEST(CanvasProject, BuiltFileNamesCollide)
{
    const std::string text = Manifest(
        "\n    { \"name\": \"A\", \"source\": \"a/x.cpkg.json\" },"
        "\n    { \"name\": \"B\", \"package\": \"b/X.cpkg\" },"
        "\n    { \"name\": \"C\", \"package\": \"c/y.cpkg\" }");
    CProjectManifest manifest;
    ASSERT_EQ(manifest.Parse(text, "p.cproj.json", nullptr), Result::Success);

    std::string built = "unchanged";
    std::vector<ProjectError> errors;
    EXPECT_EQ(manifest.FormatBuiltProject(&built, &errors), Result::CorruptedData);
    EXPECT_EQ(built, "unchanged");
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].Pointer, "/packages/1/package");
    EXPECT_EQ(errors[0].Line, 6u);
    EXPECT_EQ(errors[0].Column, 31u);
}

TEST(CanvasProject, FailureLeavesNoManifest)
{
    CProjectManifest manifest;
    ASSERT_EQ(manifest.Parse(kExample, kExamplePath, nullptr), Result::Success);
    EXPECT_EQ(manifest.Parse("{", kExamplePath, nullptr), Result::CorruptedData);
    EXPECT_TRUE(manifest.GetPackages().empty());
    EXPECT_TRUE(manifest.GetName().empty());
    size_t index = 0;
    EXPECT_EQ(manifest.FindPackage("Levels", &index), Result::NotFound);

    std::string text;
    EXPECT_EQ(manifest.FormatBuiltProject(&text, nullptr), Result::NotOpen);
    EXPECT_EQ(manifest.Parse(kExample, nullptr, nullptr), Result::InvalidArg);
}

TEST(CanvasProject, SyntaxErrors)
{
    ProjectError error = OneError("{\n  \"formatVersion\": 1,\n  \"name\": \"P\",,\n}", "");
    EXPECT_EQ(error.Line, 3u);
    EXPECT_EQ(error.Column, 15u);

    error = OneError("{\n  \"name\": \"P\",\n  \"name\": \"Q\"\n}", "");
    EXPECT_EQ(error.Line, 3u);
    EXPECT_EQ(error.Column, 3u);
}

TEST(CanvasProject, ShapeErrors)
{
    ProjectError error = OneShapeError("[]", "");
    EXPECT_EQ(error.Column, 1u);

    error = OneShapeError(R"({ "formatVersion": 1, "packages": [] })", "");
    EXPECT_NE(error.Message.find("name"), std::string::npos);

    // An unknown key is reported at the key.
    error = OneShapeError("{ \"formatVersion\": 1, \"name\": \"P\", \"packages\": [],\n  \"extra\": true }", "/extra");
    EXPECT_EQ(error.Line, 2u);
    EXPECT_EQ(error.Column, 3u);

    OneShapeError(R"({ "formatVersion": 2, "name": "P", "packages": [] })", "/formatVersion");
    OneShapeError(R"({ "formatVersion": "1", "name": "P", "packages": [] })", "/formatVersion");
    OneShapeError(R"({ "formatVersion": 1, "name": "", "packages": [] })", "/name");
    OneShapeError(R"({ "formatVersion": 1, "name": 5, "packages": [] })", "/name");
    OneShapeError(R"({ "formatVersion": 1, "name": "P", "packages": {} })", "/packages");
    OneShapeError(Manifest("", R"("output": "a\\b")"), "/output");
    OneShapeError(Manifest("", R"("worlds": "Levels/Main")"), "/worlds");
    OneShapeError(Manifest("7"), "/packages/0");
}

TEST(CanvasProject, PackageEntryErrors)
{
    OneShapeError(Manifest(R"({ "source": "a.cpkg.json" })"), "/packages/0");
    OneShapeError(Manifest(R"({ "name": "A" })"), "/packages/0");
    OneShapeError(Manifest(R"({ "name": "A", "source": "a.cpkg.json", "package": "a.cpkg" })"), "/packages/0/package");
    OneShapeError(Manifest(R"({ "name": "A", "source": "a.cpkg" })"), "/packages/0/source");
    OneShapeError(Manifest(R"({ "name": "A", "package": "a.cpkg.json" })"), "/packages/0/package");
    OneShapeError(Manifest(R"({ "name": "A", "package": "" })"), "/packages/0/package");
    OneShapeError(Manifest(R"({ "name": "A", "package": "dir\\a.cpkg" })"), "/packages/0/package");
    OneShapeError(Manifest(R"({ "name": "A/B", "package": "a.cpkg" })"), "/packages/0/name");
    OneShapeError(Manifest(R"({ "name": "", "package": "a.cpkg" })"), "/packages/0/name");
    OneShapeError(Manifest(R"({ "name": "A", "package": "a.cpkg", "kind": 1 })"), "/packages/0/kind");
    OneShapeError(Manifest(R"({ "name": "A", "package": "a.cpkg", "references": [] })"), "/packages/0/references");
    OneShapeError(Manifest(R"({ "name": "A", "package": "a.cpkg", "references": { "T/x": "A" } })"), "/packages/0/references/T~1x");
    OneShapeError(Manifest(R"({ "name": "A", "package": "a.cpkg", "references": { "T": 1 } })"), "/packages/0/references/T");
}

TEST(CanvasProject, ManifestRuleErrors)
{
    // Project names are unique; the later entry is reported.
    OneError(Manifest(R"({ "name": "A", "package": "a.cpkg" }, { "name": "A", "package": "b.cpkg" })"), "/packages/1/name");

    // Each references value is a project name.
    OneError(Manifest(R"({ "name": "A", "package": "a.cpkg", "references": { "T": "Missing" } })"), "/packages/0/references/T");

    // Each worlds entry names a project name, appears once, and is package/bundle.
    const std::string_view packages = R"({ "name": "A", "package": "a.cpkg" })";
    OneError(Manifest(packages, R"("worlds": ["B/Main"])"), "/worlds/0");
    OneError(Manifest(packages, R"("worlds": ["A/Main", "A/Main"])"), "/worlds/1");
    OneError(Manifest(packages, R"("worlds": ["A"])"), "/worlds/0");
    OneError(Manifest(packages, R"("worlds": ["A/Main/X"])"), "/worlds/0");
    OneError(Manifest(packages, R"("worlds": ["/Main"])"), "/worlds/0");

    // defaultWorld is a worlds entry.
    OneError(Manifest(packages, R"("worlds": ["A/Main"], "defaultWorld": "A/Other")"), "/defaultWorld");
    OneError(Manifest(packages, R"("defaultWorld": "A/Main")"), "/defaultWorld");

    CProjectManifest manifest;
    ASSERT_EQ(manifest.Parse(Manifest(packages, R"("worlds": ["A/Main", "A/Other"], "defaultWorld": "A/Other")"), "p.cproj.json", nullptr), Result::Success);
    EXPECT_EQ(manifest.GetDefaultWorld(), 1u);
}

TEST(CanvasProject, ReportsEveryErrorInDocumentOrder)
{
    const std::vector<ProjectError> errors = ParseErrors(Manifest(
        R"({ "name": "A", "package": "a.cpkg" }, { "name": "A", "source": "b.cpkg" })",
        R"("worlds": ["Z/Main"], "extra": 1)"));
    ASSERT_EQ(errors.size(), 4u);
    EXPECT_EQ(errors[0].Pointer, "/packages/1/name");
    EXPECT_EQ(errors[1].Pointer, "/packages/1/source");
    EXPECT_EQ(errors[2].Pointer, "/worlds/0");
    EXPECT_EQ(errors[3].Pointer, "/extra");

    // Without an error list, the result is the same.
    CProjectManifest manifest;
    EXPECT_EQ(manifest.Parse(Manifest("7"), "p.cproj.json", nullptr), Result::CorruptedData);
}

TEST_F(CanvasProjectFiles, LoadsFile)
{
    // A non-ASCII directory, and a byte order mark.
    const std::filesystem::path dir = m_Dir / std::filesystem::path(u8"Gr\u00fc\u00dfe");
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "game.cproj.json";
    {
        std::ofstream file(path, std::ios::binary);
        file << "\xEF\xBB\xBF" << kExample;
    }

    CProjectManifest manifest;
    std::vector<ProjectError> errors;
    ASSERT_EQ(manifest.Load(ToUtf8(path).c_str(), &errors), Result::Success);
    EXPECT_EQ(manifest.GetName(), "Game");
    const std::u8string expected = (dir / "levels/levels.cpkg.json").lexically_normal().generic_u8string();
    EXPECT_EQ(manifest.GetPackages()[0].FilePath, std::string(reinterpret_cast<const char *>(expected.data()), expected.size()));
}

TEST_F(CanvasProjectFiles, LoadErrors)
{
    CProjectManifest manifest;
    EXPECT_EQ(manifest.Load(ToUtf8(m_Dir / "missing.cproj.json").c_str(), nullptr), Result::NotFound);
    EXPECT_EQ(manifest.Load(ToUtf8(m_Dir).c_str(), nullptr), Result::NotFound);
    EXPECT_EQ(manifest.Load(nullptr, nullptr), Result::InvalidArg);

    const std::filesystem::path path = m_Dir / "bad.cproj.json";
    {
        std::ofstream file(path, std::ios::binary);
        file << "{\n  \"name\": 1\n}\n";
    }
    std::vector<ProjectError> errors;
    EXPECT_EQ(manifest.Load(ToUtf8(path).c_str(), &errors), Result::CorruptedData);
    ASSERT_EQ(errors.size(), 3u);
    EXPECT_EQ(errors[2].Pointer, "/name");
    EXPECT_EQ(errors[2].Line, 2u);
    EXPECT_EQ(errors[2].Column, 11u);
}
