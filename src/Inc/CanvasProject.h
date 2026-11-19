//================================================================================================
// CanvasProject - reads project manifests and resolves package names
//
// See src/CanvasProject/README.md for the manifest format and the rules this API applies.
//================================================================================================

#pragma once

#include <Gem.hpp>
#include "CanvasPkg.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Canvas
{

struct ProjectError
{
    std::string Message;
    std::string Pointer;    // JSON Pointer to the value the error concerns
    uint32_t Line = 0;
    uint32_t Column = 0;
};

struct ProjectReference
{
    std::string Name;           // the package name used in the package's paths
    std::string ProjectName;    // the project name it maps to
};

struct ProjectPackage
{
    std::string Name;           // project name
    std::string Source;         // as written; empty for a prebuilt package
    std::string Package;        // as written; empty for a package listed by source
    std::string FilePath;       // Source or Package, resolved
    std::vector<ProjectReference> References;
};

struct ProjectWorld
{
    size_t Package = 0;         // index into GetPackages()
    std::string Bundle;
};

constexpr size_t kNoWorld = SIZE_MAX;

// Holds one project manifest. Its const methods may be called from any number of threads at
// once.
class CProjectManifest
{
public:
    // Reads the manifest file at path, a UTF-8 file path. Returns Success, NotFound, IoError,
    // CorruptedData (each error appended to *pErrors when pErrors is non-null), or OutOfMemory.
    // Replaces the manifest the object held, and on failure leaves it holding none.
    Gem::Result Load(const char *path, std::vector<ProjectError> *pErrors);

    // Reads manifest text, taking path as the manifest's file path. Results as for Load.
    Gem::Result Parse(std::string_view text, const char *path, std::vector<ProjectError> *pErrors);

    const std::string &GetPath() const { return m_Path; }
    const std::string &GetName() const { return m_Name; }
    const std::string &GetOutput() const { return m_Output; }
    std::span<const ProjectPackage> GetPackages() const { return m_Packages; }
    std::span<const ProjectWorld> GetWorlds() const { return m_Worlds; }
    size_t GetDefaultWorld() const { return m_DefaultWorld; }

    // Writes the index of the package with that project name, or returns NotFound.
    Gem::Result FindPackage(std::string_view name, size_t *pIndex) const;

    // Resolves name, used in the paths of the package at index package, whose declared name is
    // declaredName, by the Package Names rules. Returns NotFound or InvalidArg.
    Gem::Result ResolvePackageName(size_t package, std::string_view declaredName, std::string_view name, size_t *pIndex) const;

    // The file name the package at index package has in the build output; empty for an index
    // out of range.
    std::string_view GetBuiltFileName(size_t package) const;

    // Writes the built project's text. Returns Success, CorruptedData when two packages have the
    // same built file name, NotOpen, or OutOfMemory.
    Gem::Result FormatBuiltProject(std::string *pText, std::vector<ProjectError> *pErrors) const;

private:
    struct Location
    {
        std::string Pointer;
        uint32_t Line = 0;
        uint32_t Column = 0;
    };

    void Clear();

    bool m_Loaded = false;
    std::string m_Path;
    std::string m_Name;
    std::string m_Output;
    std::vector<ProjectPackage> m_Packages;
    std::vector<ProjectWorld> m_Worlds;
    size_t m_DefaultWorld = kNoWorld;
    std::unordered_map<std::string, size_t, CpkgDetail::NameHash, std::equal_to<>> m_PackageIndex;

    // The manifest as read, for FormatBuiltProject, kept as text so this header needs no JSON
    // library, and the location of each package's source or package value.
    std::string m_Text;
    std::vector<Location> m_FileLocations;
};

} // namespace Canvas
