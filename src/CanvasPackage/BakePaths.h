//================================================================================================
// BakePaths - path resolution for the bake step.
//
// Manifest paths may be relative (resolved against the manifest's directory) or absolute (used
// as-is). ResolveBakePaths establishes the output location; ResolveManifestRelative resolves one
// manifest-relative path to absolute; MakePkgRelative re-expresses an absolute path relative to the
// output .cpkg directory, which is how texture paths are stored in the TXTR chunk so a package and
// its external textures relocate together. Internal to CanvasPackage; not part of the public Inc/
// surface. See README.md "Path resolution rule".
//================================================================================================
#pragma once

#include "BakeManifest.h"
#include "CanvasPackageData.h" // Canvas::PackageLogFn, Gem::Result

#include <filesystem>
#include <string>

namespace Canvas::Cpkg
{

//--------------------------------------------------------------------------------------------------
// ResolvedPaths - the absolute anchor locations derived from a manifest and its file path.
//--------------------------------------------------------------------------------------------------
struct ResolvedPaths
{
    std::filesystem::path manifestDir;   // absolute directory containing the manifest
    std::filesystem::path outputAbsPath; // absolute path of the .cpkg to write
    std::filesystem::path outputDir;     // absolute directory of the .cpkg
};

// Resolve the manifest's output path against the manifest's directory (absolute output paths are
// used as-is) and fill *out. pManifestPath is UTF-8. Fails with BadPointer on a null argument and
// InvalidArg when the manifest has no output path.
Gem::Result ResolveBakePaths(const BakeManifest& manifest, const char* pManifestPath,
                             ResolvedPaths* out, const PackageLogFn& logFn = {});

// Resolve one manifest-relative path (UTF-8) to an absolute, lexically-normalized UTF-8 path. An
// already-absolute relPath is normalized and returned as-is.
std::string ResolveManifestRelative(const ResolvedPaths& paths, const std::string& relPath);

// Re-express an absolute UTF-8 path relative to the output .cpkg directory, as a UTF-8,
// forward-slash string (the TXTR on-disk convention).
std::string MakePkgRelative(const ResolvedPaths& paths, const std::string& absPath);

// Return the path as a string in Canvas's form: UTF-8, forward slashes.
std::string PathToString(const std::filesystem::path& path);

} // namespace Canvas::Cpkg
