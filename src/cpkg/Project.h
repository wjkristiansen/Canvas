//================================================================================================
// Project - an authored project and what its packages' names and asset types say
//
// See "Project Context" and "Project Builds" in src/cpkg/README.md.
//================================================================================================

#pragma once

#include "CpkgTool.h"
#include "Diagnostics.h"

#include "CanvasPkg.h"
#include "CanvasProject.h"
#include "JsonText.h"

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CpkgTool
{

// One package of a project as paths from other packages see it: its bundles, and the names and
// types of their shared assets.
struct ProjectPackageNames
{
    struct Bundle
    {
        bool World = false;         // holds a WorldSettings asset
        bool Imports = false;       // has imports, which may build assets it does not declare
        std::vector<std::pair<std::string, Canvas::AssetType>> Assets;
    };

    bool Readable = false;          // its source loaded without error, or its .cpkg file opened
    Gem::Result OpenResult = Gem::Result::Success;     // for a prebuilt package, what opening it returned
    std::vector<std::string> Inputs;                    // the files reading it read
    std::map<std::string, Bundle, std::less<>> Bundles;
};

// An authored project: its manifest, and its packages' names, each read the first time it is
// asked for. Its const methods may be called concurrently.
class CProject
{
public:
    CProject() = default;
    CProject(const CProject &) = delete;
    CProject &operator=(const CProject &) = delete;

    // Loads the manifest at path, a UTF-8 path, and reports its errors. Returns true when it is
    // valid.
    bool Load(const std::string &path, CDiagnostics &diagnostics);

    const Canvas::CProjectManifest &GetManifest() const { return m_Manifest; }

    // The index of the package whose source is the package file at path, a UTF-8 path, or
    // kNoPackage.
    size_t FindSource(const std::string &path) const;
    static constexpr size_t kNoPackage = SIZE_MAX;

    // The names of the package at index, read on first use.
    const ProjectPackageNames &GetNames(size_t index) const;

    // Reports a finding at the manifest's value at pointer, a JSON Pointer.
    void Report(CDiagnostics &diagnostics, Severity severity, const std::string &pointer, std::string_view message) const;

    // The JSON Pointer of the package entry's "source" or "package" value.
    static std::string FilePointer(const Canvas::ProjectPackage &package, size_t index);

private:
    struct Slot
    {
        std::once_flag Once;
        ProjectPackageNames Names;
    };

    void Read(size_t index, ProjectPackageNames *pNames) const;

    Canvas::CProjectManifest m_Manifest;
    std::string m_Text;
    nlohmann::ordered_json m_Document;
    std::unique_ptr<Canvas::CJsonLineIndex> m_Lines;
    std::vector<std::unique_ptr<Slot>> m_Slots;
};

// What a path into another package names, as the project decides it.
struct ProjectTarget
{
    enum class Kind
    {
        Found,
        NoPackage,      // the package name names no package of the project
        Unreadable,     // the package's source has an error, or its .cpkg file does not open
        NoBundle,
        WorldBundle,    // a bundle path names a world bundle
        NoAsset,
        OtherType,      // the asset is of Type, not the type the path requires
        ByImport,       // the bundle declares no such asset, and has imports that may build it
    };

    Kind State = Kind::Found;
    Canvas::AssetType Type = Canvas::AssetType::Unknown;
    std::string Package;    // the project name of the package the path names
};

// Resolves the paths of one package of a project into its other packages, and records the files
// that reading them reads. One package's validation uses one lookup.
class CProjectLookup
{
public:
    // package is the index of the package whose paths are resolved, and declaredName its declared
    // name.
    CProjectLookup(const CProject &project, size_t package, std::string declaredName);

    // What the bundle path packageName/bundle names. A placement's or region's bundle path
    // names no world bundle.
    ProjectTarget FindBundle(std::string_view packageName, std::string_view bundle);

    // What the asset path packageName/bundle/asset names, for a path that requires type.
    ProjectTarget FindAsset(std::string_view packageName, std::string_view bundle, std::string_view asset, Canvas::AssetType type);

    // The manifest and the files of each package it read, in the order it first read them.
    const std::vector<std::string> &GetInputs() const { return m_Inputs; }

private:
    const ProjectPackageNames *Resolve(std::string_view packageName, ProjectTarget *pTarget);

    const CProject &m_Project;
    size_t m_Package;
    std::string m_DeclaredName;
    std::vector<std::string> m_Inputs;
    std::vector<bool> m_Read;   // per package of the project
};

struct SourcePackage;

// Runs the stages before Write on the package source at packageFile, validating its paths into
// other packages against the authored project at projectFile, when given. Returns true when they
// reported no error.
bool LoadAndValidate(const std::string &packageFile, const std::optional<std::string> &projectFile, SourcePackage *pPackage,
    CDiagnostics &diagnostics);

// Builds an authored project, as "Project Builds" in src/cpkg/README.md defines.
void BuildProject(const BuildOptions &options, CDiagnostics &diagnostics);

// Validates an authored project and every package it lists by source.
void ValidateProject(const ValidateOptions &options, CDiagnostics &diagnostics);

} // namespace CpkgTool
