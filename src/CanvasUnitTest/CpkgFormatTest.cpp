//================================================================================================
// CpkgFormatTest - Tests for cpkg's format command
//================================================================================================

#include "pch.h"
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

    // A package file and a bundle file it includes, with fields equal to their defaults, numbers
    // spelled at length, and a layout other than the standard form's.
    const char *const kPackage = R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Looks",
      "$comment": "kept",
      "materials": {
        "Plain": { "baseColor": [1, 1, 1, 1], "roughness": 0.50000000, "metallic": 0.0, "$extras": { "x": 0.10000000149 } },
        "Shiny": { "roughness": 1e-1, "emissive": [0, 0, 0] }
      },
      "nodes": [
        { "name": "Root", "translation": [0, 0, 0], "rotation": { "degrees": [0, 0, 0] }, "scale": [1.0, 1, 1], "mobility": "static",
          "light": { "type": "point", "enabled": true, "castsShadows": false, "intensity": 2.0 } }
      ]
    },
    { "include": "Rig.cbundle.json" }
  ]
}
)";

    const char *const kBundle = R"({ "formatVersion": 1, "bundle": { "name": "Rig",
  "placements": [ { "bundle": "Looks", "mobility": "movable", "rotation": [0, 0, 0, 1], "translation": [-0.0, 0, 2.0] } ],
  "imports": [ { "file": "d.fbx", "scale": 1, "nodes": true, "edits": { "D": { "translation": [0, 0, 0], "light": { "enabled": true } } } } ] } }
)";

    // The two files in the standard form: an edit's fields and -0, which is not the stored 0,
    // stay, as do $comment and $extras as written.
    const char *const kStandardPackage = R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Looks",
      "$comment": "kept",
      "materials": {
        "Plain": { "roughness": 0.5, "$extras": { "x": 0.10000000149 } },
        "Shiny": { "roughness": 0.1 }
      },
      "nodes": [{ "name": "Root", "light": { "type": "point", "intensity": 2 } }]
    },
    { "include": "Rig.cbundle.json" }
  ]
}
)";

    const char *const kStandardBundle = R"({
  "formatVersion": 1,
  "bundle": {
    "name": "Rig",
    "placements": [{ "bundle": "Looks", "mobility": "movable", "translation": [-0.0, 0, 2] }],
    "imports": [
      {
        "file": "d.fbx",
        "edits": { "D": { "translation": [0, 0, 0], "light": { "enabled": true } } }
      }
    ]
  }
}
)";
}

//------------------------------------------------------------------------------------------------
// Runs each test in its own empty current directory, so paths in diagnostics are relative to it.
class CpkgFormat : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo *pInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        m_Dir = std::filesystem::temp_directory_path() / (std::string("CpkgFormat_") + pInfo->name());
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

TEST_F(CpkgFormat, RewritesInTheStandardForm)
{
    WriteText("levels.cpkg.json", kPackage);
    WriteText("Rig.cbundle.json", kBundle);

    // A package file is rewritten, and the bundle files it includes are left as they are.
    const RunResult package = RunCpkg({ "format", "levels.cpkg.json" });
    EXPECT_EQ(package.Err, "");
    ASSERT_EQ(package.Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(ReadBytes("levels.cpkg.json"), kStandardPackage);
    EXPECT_EQ(ReadBytes("Rig.cbundle.json"), kBundle);

    // A bundle file is rewritten on its own.
    const RunResult bundle = RunCpkg({ "format", "Rig.cbundle.json" });
    EXPECT_EQ(bundle.Err, "");
    ASSERT_EQ(bundle.Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(ReadBytes("Rig.cbundle.json"), kStandardBundle);
}

TEST_F(CpkgFormat, ReadsOnlyTheFileItFormats)
{
    // The bundle file the package file includes is absent.
    WriteText("levels.cpkg.json", kPackage);
    const RunResult formatted = RunCpkg({ "format", "levels.cpkg.json" });
    EXPECT_EQ(formatted.Err, "");
    ASSERT_EQ(formatted.Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(ReadBytes("levels.cpkg.json"), kStandardPackage);
}

TEST_F(CpkgFormat, SavesOnlyAFileWhoseTextChanges)
{
    WriteText("levels.cpkg.json", kStandardPackage);
    const auto written = std::filesystem::last_write_time("levels.cpkg.json");
    std::filesystem::last_write_time("levels.cpkg.json", written - std::chrono::hours(1));
    const auto earlier = std::filesystem::last_write_time("levels.cpkg.json");

    ASSERT_EQ(RunCpkg({ "format", "levels.cpkg.json" }).Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(std::filesystem::last_write_time("levels.cpkg.json"), earlier);
}

TEST_F(CpkgFormat, CheckReportsAFileNotInTheStandardForm)
{
    WriteText("levels.cpkg.json", kStandardPackage);
    WriteText("Rig.cbundle.json", kBundle);

    const RunResult checked = RunCpkg({ "format", "Rig.cbundle.json", "--check" });
    EXPECT_EQ(checked.Err, "Rig.cbundle.json: error: the file is not in the standard form; cpkg format rewrites it\n");
    EXPECT_EQ(checked.Status, CpkgTool::kExitError);
    EXPECT_EQ(ReadBytes("Rig.cbundle.json"), kBundle);

    // The package file is in the standard form, whatever the files it includes hold.
    const RunResult package = RunCpkg({ "format", "levels.cpkg.json", "--check" });
    EXPECT_EQ(package.Err, "");
    EXPECT_EQ(package.Status, CpkgTool::kExitSuccess);

    WriteText("Rig.cbundle.json", kStandardBundle);
    const RunResult clean = RunCpkg({ "format", "Rig.cbundle.json", "--check" });
    EXPECT_EQ(clean.Err, "");
    EXPECT_EQ(clean.Status, CpkgTool::kExitSuccess);
}

TEST_F(CpkgFormat, SavesNothingWhenLoadingReportsAnError)
{
    const std::string package = std::string(kPackage).replace(std::string(kPackage).find("\"static\""), 8, "\"fixed\"");
    WriteText("levels.cpkg.json", package);

    const RunResult formatted = RunCpkg({ "format", "levels.cpkg.json" });
    EXPECT_NE(formatted.Err.find("levels.cpkg.json:13:"), std::string::npos) << formatted.Err;
    EXPECT_EQ(formatted.Status, CpkgTool::kExitError);
    EXPECT_EQ(ReadBytes("levels.cpkg.json"), package);
}

TEST_F(CpkgFormat, UsageErrors)
{
    const char *pHelp = "Run 'cpkg --help' for usage.\n";
    const RunResult missing = RunCpkg({ "format" });
    EXPECT_EQ(missing.Err, std::string("cpkg: error: format: missing argument <file>\n") + pHelp);
    EXPECT_EQ(missing.Status, CpkgTool::kExitUsage);

    const RunResult wrong = RunCpkg({ "format", "levels.cpkg" });
    EXPECT_EQ(wrong.Err, std::string("cpkg: error: format: expected a .cpkg.json or .cbundle.json file: levels.cpkg\n") + pHelp);
    EXPECT_EQ(wrong.Status, CpkgTool::kExitUsage);
}
