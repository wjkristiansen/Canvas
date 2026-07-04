//================================================================================================
// BakeManifest - the parsed JSON bake manifest that drives CanvasBake.
//
// A manifest names the output .cpkg, the source scenes (FBX in v1), and any explicitly declared
// textures. LoadBakeManifest parses the JSON and stores every path exactly as written (relative or
// absolute); path resolution happens later in BakePaths. Internal to CanvasPackage; not part of the
// public Inc/ surface. See README.md "Build Manifest (JSON)" for the on-disk schema.
//================================================================================================
#pragma once

#include "CanvasPackageData.h" // Canvas::PackageLogFn, Gem::Result

#include <string>
#include <vector>

namespace Canvas::Cpkg
{

//--------------------------------------------------------------------------------------------------
// ManifestTexture - one explicitly declared image asset from the manifest's "textures" array.
//--------------------------------------------------------------------------------------------------
struct ManifestTexture
{
    std::string name;          // runtime lookup key; empty = unnamed
    std::string path;          // manifest-relative or absolute, as written
    bool        embed = false; // embed decoded bytes; resolved against defaultEmbed at load
};

//--------------------------------------------------------------------------------------------------
// ManifestSource - one source scene from the manifest's "sources" array.
//--------------------------------------------------------------------------------------------------
struct ManifestSource
{
    std::string              type;               // "fbx" is the only supported type in v1
    std::string              path;               // manifest-relative or absolute, as written
    std::vector<std::string> textureSearchPaths; // manifest-relative directories, searched in order
};

//--------------------------------------------------------------------------------------------------
// BakeManifest - the whole manifest, paths stored verbatim.
//--------------------------------------------------------------------------------------------------
struct BakeManifest
{
    std::string                 name;
    std::string                 outputPath;    // path of the .cpkg to write ("output" field)
    std::vector<ManifestSource> sources;
    std::vector<ManifestTexture> textures;
    bool                        defaultEmbed = false;
};

// Parse the JSON manifest at pManifestPath (UTF-8) into *out. Paths are stored as written; each
// texture's embed defaults to the manifest's defaultEmbed when the field is omitted. Fails with
// BadPointer on a null argument, NotFound when the file cannot be opened, and InvalidArg on
// malformed JSON or a missing required field ("output").
Gem::Result LoadBakeManifest(const char* pManifestPath, BakeManifest* out,
                             const PackageLogFn& logFn = {});

} // namespace Canvas::Cpkg
