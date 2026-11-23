//================================================================================================
// CpkgProjectTest - Tests for cpkg's project context and project builds
//================================================================================================

#include "pch.h"
#include "CanvasPkg.h"
#include "CpkgTool.h"
#include "ImageFiles.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    void WriteBytes(const std::string &path, std::string_view bytes)
    {
        const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
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

    std::vector<std::string> FilesUnder(const std::string &directory)
    {
        std::vector<std::string> files;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(directory))
        {
            if (entry.is_regular_file())
                files.push_back(entry.path().lexically_relative(directory).generic_string());
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    std::filesystem::file_time_type TimeOf(const std::string &path)
    {
        return std::filesystem::last_write_time(std::filesystem::path(path));
    }

    // Moves the file's modification time an hour back, so a later write is seen as a change
    // whatever the file system's time resolution.
    void Age(const std::string &path)
    {
        std::filesystem::last_write_time(std::filesystem::path(path), TimeOf(path) - std::chrono::hours(1));
    }

    // A project of three packages: Levels and Textures listed by source, and Vendor prebuilt.
    void WriteProject()
    {
        WriteBytes("game.cproj.json", R"({
  "formatVersion": 1,
  "name": "Game",
  "packages": [
    { "name": "Levels", "source": "levels/levels.cpkg.json" },
    { "name": "Textures", "source": "textures/textures.cpkg.json" },
    { "name": "Vendor", "package": "vendor/vendor.cpkg" }
  ],
  "worlds": [ "Levels/Town" ]
}
)");
        WriteBytes("levels/levels.cpkg.json", R"({ "formatVersion": 1, "packageName": "Levels", "bundles": [
  { "name": "Looks", "materials": { "Brass": { "textures": { "albedo": "Textures/Metals/Brass" } },
                                    "Crate": { "textures": { "albedo": "Vendor/Wood/Oak" } } } },
  { "name": "Town", "world": {}, "placements": [ { "bundle": "Vendor/Props" } ] }
] }
)");
        WriteBytes("textures/textures.cpkg.json", R"({ "formatVersion": 1, "packageName": "Textures", "bundles": [
  { "name": "Metals", "images": { "Brass": "brass.png" } }
] }
)");
        WriteBytes("textures/brass.png", ImageFiles::Png(4, 4));

        // The prebuilt package, built from a source the project does not list.
        WriteBytes("vendor/src/vendor.cpkg.json", R"({ "formatVersion": 1, "packageName": "Vendor", "bundles": [
  { "name": "Wood", "images": { "Oak": "oak.png" } },
  { "name": "Props", "nodes": [ { "name": "Root" } ] },
  { "name": "Yard", "world": {} }
] }
)");
        WriteBytes("vendor/src/oak.png", ImageFiles::Png(2, 2));
        ASSERT_EQ(RunCpkg({ "build", "vendor/src/vendor.cpkg.json", "-o", "vendor/vendor.cpkg" }).Err, "");
    }
}

//------------------------------------------------------------------------------------------------
// Runs each test in its own empty current directory, so paths in diagnostics are relative to it.
class CpkgProject : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo *pInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        m_Dir = std::filesystem::temp_directory_path() / (std::string("CpkgProject_") + pInfo->name());
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

TEST_F(CpkgProject, ChecksPathsIntoOtherPackages)
{
    WriteProject();
    WriteBytes("levels/levels.cpkg.json", R"({ "formatVersion": 1, "packageName": "Levels", "bundles": [
  { "name": "Looks", "materials": {
    "A": { "textures": { "albedo": "Textures/Metals/Brass" } },
    "B": { "textures": { "albedo": "Textures/Metals/Steel" } },
    "C": { "textures": { "albedo": "Textures/Woods/Oak" } },
    "D": { "textures": { "albedo": "Ghost/Metals/Brass" } },
    "E": { "textures": { "albedo": "Vendor/Wood/Oak" } },
    "F": { "textures": { "albedo": "Vendor/Wood/Pine" } } },
    "skyboxes": { "Sky": { "image": "Textures/Metals/Brass" } },
    "meshes": { "M": { "groups": [ { "material": "Vendor/Wood/Oak", "positions": [0, 0, 0], "normals": [0, 0, 1] } ] } } },
  { "name": "Town", "world": { "regions": [ { "region": "Vendor/Yard", "bounds": { "min": [0, 0, 0], "max": [1, 1, 1] } } ] },
    "placements": [ { "bundle": "Vendor/Props" }, { "bundle": "Vendor/Shed" } ] }
] }
)");

    // Without the project, paths into other packages are not checked.
    const RunResult alone = RunCpkg({ "validate", "levels/levels.cpkg.json" });
    EXPECT_EQ(alone.Err, "");
    EXPECT_EQ(alone.Status, CpkgTool::kExitSuccess);

    const RunResult result = RunCpkg({ "validate", "levels/levels.cpkg.json", "--project", "game.cproj.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels/levels.cpkg.json:4:36: error: the bundle \"Textures/Metals\" has no asset \"Steel\" [/bundles/0/materials/B/textures/albedo]\n"
        "levels/levels.cpkg.json:5:36: error: the package \"Textures\" has no bundle \"Woods\" [/bundles/0/materials/C/textures/albedo]\n"
        "levels/levels.cpkg.json:6:36: error: the project has no package \"Ghost\" [/bundles/0/materials/D/textures/albedo]\n"
        "levels/levels.cpkg.json:8:36: error: the bundle \"Vendor/Wood\" has no asset \"Pine\" [/bundles/0/materials/F/textures/albedo]\n"
        "levels/levels.cpkg.json:10:50: error: \"Vendor/Wood/Oak\" is an image, not a material [/bundles/0/meshes/M/groups/0/material]\n"
        "levels/levels.cpkg.json:11:57: error: \"Vendor/Yard\" is a world bundle, which a bundle path cannot name [/bundles/1/world/regions/0/region]\n"
        "levels/levels.cpkg.json:12:63: error: the package \"Vendor\" has no bundle \"Shed\" [/bundles/1/placements/1/bundle]\n");
}

TEST_F(CpkgProject, ReportsAPackageTheProjectDoesNotList)
{
    WriteProject();
    WriteBytes("other.cpkg.json", R"({ "formatVersion": 1, "packageName": "Other", "bundles": [ { "name": "Empty" } ] })");
    const RunResult result = RunCpkg({ "build", "other.cpkg.json", "--project", "game.cproj.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err, "other.cpkg.json: error: the project \"game.cproj.json\" lists no package with this source\n");
    EXPECT_FALSE(std::filesystem::exists("other.cpkg"));
}

TEST_F(CpkgProject, ReportsAnUnreadablePackageOnce)
{
    WriteProject();
    WriteBytes("textures/textures.cpkg.json", R"({ "formatVersion": 1, "packageName": "Textures", "bundles": [ { "name": "Metals", "bogus": 1 } ] })");
    WriteBytes("levels/levels.cpkg.json", R"({ "formatVersion": 1, "packageName": "Levels", "bundles": [
  { "name": "Looks", "materials": {
    "A": { "textures": { "albedo": "Textures/Metals/Brass" } },
    "B": { "textures": { "albedo": "Textures/Metals/Steel" } } } }
] }
)");
    const RunResult result = RunCpkg({ "validate", "levels/levels.cpkg.json", "--project", "game.cproj.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels/levels.cpkg.json:3:36: error: the package \"Textures\" cannot be read, so paths into it are not checked [/bundles/0/materials/A/textures/albedo]\n");
}

TEST_F(CpkgProject, BuildsAProject)
{
    WriteProject();
    const RunResult result = RunCpkg({ "build", "game.cproj.json" });
    EXPECT_EQ(result.Err, "");
    ASSERT_EQ(result.Status, CpkgTool::kExitSuccess);

    EXPECT_EQ(FilesUnder("build"), (std::vector<std::string>{ "game.cproj.json", "levels.cpkg", "levels.cpkg.inputs", "textures.cpkg",
        "textures.cpkg.inputs", "vendor.cpkg" }));
    EXPECT_EQ(ReadBytes("build/game.cproj.json"), R"({
  "formatVersion": 1,
  "name": "Game",
  "packages": [
    { "name": "Levels", "package": "levels.cpkg" },
    { "name": "Textures", "package": "textures.cpkg" },
    { "name": "Vendor", "package": "vendor.cpkg" }
  ],
  "worlds": ["Levels/Town"]
}
)");
    EXPECT_EQ(ReadBytes("build/vendor.cpkg"), ReadBytes("vendor/vendor.cpkg"));

    // Each package builds as a build of the package alone with the project would.
    ASSERT_EQ(RunCpkg({ "build", "levels/levels.cpkg.json", "--project", "game.cproj.json", "-o", "levels.cpkg" }).Err, "");
    EXPECT_EQ(ReadBytes("build/levels.cpkg"), ReadBytes("levels.cpkg"));

    // The record names the version, each input, and the package last.
    const std::string record = ReadBytes("build/textures.cpkg.inputs");
    std::istringstream lines(record);
    std::vector<std::string> paths;
    std::string line;
    std::getline(lines, line);
    EXPECT_EQ(line, "cpkg " + std::string(CPKG_TEST_VERSION));
    while (std::getline(lines, line))
        paths.push_back(line.substr(line.find(' ', line.find(' ') + 1) + 1));
    const std::string here = std::filesystem::current_path().generic_string();
    EXPECT_EQ(paths, (std::vector<std::string>{ here + "/textures/textures.cpkg.json", here + "/textures/brass.png", here + "/game.cproj.json",
        here + "/build/textures.cpkg" }));
}

TEST_F(CpkgProject, RebuildsOnlyWhatChanged)
{
    WriteProject();
    ASSERT_EQ(RunCpkg({ "build", "game.cproj.json" }).Err, "");
    for (const char *file : { "build/game.cproj.json", "build/levels.cpkg", "build/levels.cpkg.inputs", "build/textures.cpkg",
             "build/textures.cpkg.inputs", "build/vendor.cpkg" })
        Age(file);
    auto times = []()
    {
        std::vector<std::filesystem::file_time_type> result;
        for (const std::string &file : FilesUnder("build"))
            result.push_back(TimeOf("build/" + file));
        return result;
    };

    // The aged .cpkg files no longer match their records, so their packages build again, to the
    // same bytes, leaving each .cpkg file untouched and writing its record again.
    const std::vector<std::filesystem::file_time_type> before = times();
    ASSERT_EQ(RunCpkg({ "build", "game.cproj.json" }).Err, "");
    const std::vector<std::filesystem::file_time_type> rebuilt = times();
    EXPECT_EQ(rebuilt[0], before[0]);       // the built project is unchanged
    EXPECT_NE(rebuilt[2], before[2]);       // the stale records are written again
    EXPECT_EQ(rebuilt[1], before[1]);       // with each .cpkg file unchanged

    // With nothing changed, the build writes nothing.
    ASSERT_EQ(RunCpkg({ "build", "game.cproj.json" }).Err, "");
    EXPECT_EQ(times(), rebuilt);

    // A changed input builds only its package again.
    WriteBytes("textures/brass.png", ImageFiles::Png(8, 8));
    ASSERT_EQ(RunCpkg({ "build", "game.cproj.json" }).Err, "");
    const std::vector<std::filesystem::file_time_type> changed = times();
    EXPECT_EQ(changed[1], rebuilt[1]);      // levels.cpkg
    EXPECT_EQ(changed[2], rebuilt[2]);      // levels.cpkg.inputs
    EXPECT_NE(changed[3], rebuilt[3]);      // textures.cpkg
    EXPECT_NE(changed[4], rebuilt[4]);      // textures.cpkg.inputs

    // A changed manifest is an input of every package.
    Age("build/levels.cpkg.inputs");
    const std::filesystem::file_time_type aged = TimeOf("build/levels.cpkg.inputs");
    WriteBytes("game.cproj.json", ReadBytes("game.cproj.json") + "\n");
    ASSERT_EQ(RunCpkg({ "build", "game.cproj.json" }).Err, "");
    EXPECT_NE(TimeOf("build/levels.cpkg.inputs"), aged);
}

TEST_F(CpkgProject, ReportsAnUpToDatePackagesWarningsAgain)
{
    WriteProject();
    WriteBytes("textures/brass.png", ImageFiles::Jpeg(4, 4));
    const std::string warning =
        "textures/textures.cpkg.json:2:44: warning: the extension names a PNG image, but the file holds a JPEG image: \"brass.png\" [/bundles/0/images/Brass]\n";

    const RunResult first = RunCpkg({ "build", "game.cproj.json" });
    EXPECT_EQ(first.Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(first.Err, warning);
    const std::filesystem::file_time_type built = TimeOf("build/textures.cpkg.inputs");

    const RunResult second = RunCpkg({ "build", "game.cproj.json" });
    EXPECT_EQ(second.Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(second.Err, warning);
    EXPECT_EQ(TimeOf("build/textures.cpkg.inputs"), built);

    // With --werror the recorded warning fails the build, which then builds the package again.
    const RunResult strict = RunCpkg({ "--werror", "build", "game.cproj.json" });
    EXPECT_EQ(strict.Status, CpkgTool::kExitError);
    EXPECT_EQ(strict.Err, warning.substr(0, warning.find("warning")) + "error" + warning.substr(warning.find("warning") + 7));
    EXPECT_FALSE(std::filesystem::exists("build/textures.cpkg.inputs"));
    EXPECT_FALSE(std::filesystem::exists("build/game.cproj.json"));
    EXPECT_EQ(RunCpkg({ "--werror", "build", "game.cproj.json" }).Err, strict.Err);
}

TEST_F(CpkgProject, AFailedBuildWritesNoBuiltProject)
{
    WriteProject();
    ASSERT_EQ(RunCpkg({ "build", "game.cproj.json" }).Err, "");
    const std::string levels = ReadBytes("build/levels.cpkg");

    WriteBytes("textures/textures.cpkg.json", R"({ "formatVersion": 1, "packageName": "Textures", "bundles": [ { "name": "Metals", "bogus": 1 } ] })");
    const RunResult result = RunCpkg({ "build", "game.cproj.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels/levels.cpkg.json:2:72: error: the package \"Textures\" cannot be read, so paths into it are not checked [/bundles/0/materials/Brass/textures/albedo]\n"
        "textures/textures.cpkg.json:1:83: error: unknown key \"bogus\" [/bundles/0/bogus]\n");
    EXPECT_FALSE(std::filesystem::exists("build/game.cproj.json"));
    EXPECT_FALSE(std::filesystem::exists("build/textures.cpkg.inputs"));
    EXPECT_FALSE(std::filesystem::exists("build/levels.cpkg.inputs"));
    EXPECT_EQ(ReadBytes("build/levels.cpkg"), levels);
}

TEST_F(CpkgProject, ReportsInManifestOrderForEveryJobCount)
{
    WriteProject();
    WriteBytes("levels/levels.cpkg.json", R"({ "formatVersion": 1, "packageName": "Levels", "bundles": [ { "name": "Town", "world": {}, "bogus": 1 } ] })");
    WriteBytes("textures/textures.cpkg.json", R"({ "formatVersion": 1, "packageName": "Textures", "bundles": [ { "name": "Metals", "bogus": 2 } ] })");
    const RunResult one = RunCpkg({ "build", "game.cproj.json", "-j", "1" });
    EXPECT_EQ(one.Status, CpkgTool::kExitError);
    EXPECT_EQ(one.Err,
        "levels/levels.cpkg.json:1:92: error: unknown key \"bogus\" [/bundles/0/bogus]\n"
        "textures/textures.cpkg.json:1:83: error: unknown key \"bogus\" [/bundles/0/bogus]\n");
    for (const char *jobs : { "2", "8" })
        EXPECT_EQ(RunCpkg({ "build", "game.cproj.json", "-j", jobs }).Err, one.Err) << jobs;
    EXPECT_EQ(RunCpkg({ "validate", "game.cproj.json" }).Err, one.Err);
}

TEST_F(CpkgProject, ValidatesTheProjectsOwnContent)
{
    WriteProject();
    WriteBytes("game.cproj.json", R"({
  "formatVersion": 1,
  "name": "Game",
  "packages": [
    { "name": "Levels", "source": "levels/levels.cpkg.json" },
    { "name": "Textures", "source": "textures/textures.cpkg.json" },
    { "name": "Vendor", "package": "vendor/vendor.cpkg" },
    { "name": "Broken", "package": "broken.cpkg" }
  ],
  "worlds": [ "Levels/Town", "Levels/Looks", "Vendor/Hall", "Vendor/Yard" ]
}
)");
    WriteBytes("broken.cpkg", "not a package");
    const RunResult result = RunCpkg({ "validate", "game.cproj.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "game.cproj.json:8:36: error: not a package file [/packages/3/package]\n"
        "game.cproj.json:10:30: error: \"Levels/Looks\" is not a world bundle [/worlds/1]\n"
        "game.cproj.json:10:46: error: the package \"Vendor\" has no bundle \"Hall\" [/worlds/2]\n");
    EXPECT_EQ(RunCpkg({ "build", "game.cproj.json" }).Err, result.Err);
    EXPECT_FALSE(std::filesystem::exists("build/game.cproj.json"));
}

TEST_F(CpkgProject, WritesToTheOutputDirectoryItIsGiven)
{
    WriteProject();
    ASSERT_EQ(RunCpkg({ "build", "game.cproj.json", "-o", "out/content" }).Err, "");
    EXPECT_TRUE(std::filesystem::exists("out/content/game.cproj.json"));
    EXPECT_FALSE(std::filesystem::exists("build"));
}

#if defined(_WIN32)
// Names that differ in the case of a non-ASCII letter pass CanvasProject's comparison, and are one
// name on a file system that ignores case.
TEST_F(CpkgProject, ReportsBuiltFileNamesTheFileSystemTakesAsOne)
{
    WriteBytes("game.cproj.json", R"({
  "formatVersion": 1,
  "name": "Game",
  "packages": [
    { "name": "Upper", "source": "\u00c4.cpkg.json" },
    { "name": "Lower", "package": "vendor/\u00e4.cpkg" }
  ]
}
)");
    const RunResult result = RunCpkg({ "build", "game.cproj.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "game.cproj.json:6:39: error: the output directory's file system holds the built file name \"\xC3\xA4.cpkg\" of an earlier package [/packages/1/package]\n");
    EXPECT_EQ(FilesUnder("build"), std::vector<std::string>{});
}
#endif

TEST_F(CpkgProject, UsageErrors)
{
    const char *pHelp = "Run 'cpkg --help' for usage.\n";
    EXPECT_EQ(RunCpkg({ "build", "a.cpkg.json", "-j", "2" }).Err, std::string("cpkg: error: build: --jobs applies to a project, not a package\n") + pHelp);
    EXPECT_EQ(RunCpkg({ "build", "a.cproj.json", "--project", "b.cproj.json" }).Err,
        std::string("cpkg: error: build: --project applies to a package, not a project\n") + pHelp);
    EXPECT_EQ(RunCpkg({ "validate", "a.cpkg.json", "--project", "b.json" }).Err,
        std::string("cpkg: error: validate: expected a .cproj.json file for --project: b.json\n") + pHelp);
    EXPECT_EQ(RunCpkg({ "build", "a.cproj.json", "-j", "0" }).Err, std::string("cpkg: error: build: --jobs takes a count of at least 1: 0\n") + pHelp);
    EXPECT_EQ(RunCpkg({ "build", "a.cproj.json", "-j", "x" }).Status, CpkgTool::kExitUsage);
}
