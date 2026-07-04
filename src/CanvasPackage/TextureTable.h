//================================================================================================
// TextureTable - the deduplicated texture list built during bake.
//
// Manifest-declared textures are added first, then FBX-sourced ones; entries are deduplicated by
// resolved absolute path, so a texture referenced by both a manifest and an FBX material yields one
// TXTR entry (the manifest entry, added first, keeps its Name). Finalize produces the PackageTexture
// list; embedding of pixel bytes happens later in the bake tool. Internal to CanvasPackage; not part
// of the public Inc/ surface. See README.md "Bake-time texture resolution order".
//================================================================================================
#pragma once

#include "BakeManifest.h"
#include "BakePaths.h"
#include "CanvasPackageData.h" // Canvas::PackageTexture

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Canvas::Cpkg
{

class TextureTable
{
public:
    // Add a manifest-declared texture, resolving its path against the manifest directory. Returns
    // the entry's TXTR index; an already-present resolved path returns the existing index unchanged.
    int32_t AddManifestTexture(const ManifestTexture& texture, const ResolvedPaths& paths);

    // Add an FBX-sourced texture by its already-resolved absolute path (UTF-8). Deduplicates against
    // prior entries; a new entry has an empty Name. Returns the entry's TXTR index.
    int32_t AddFbxTexture(const std::string& absPath, const ResolvedPaths& paths);

    // Find the TXTR index of a previously added texture by its absolute path (UTF-8), or -1 if
    // absent.
    int32_t FindByAbsPath(const std::string& absPath) const;

    // The final PackageTexture list, in add order. Format/extents stay at their defaults and Bytes
    // is empty; the bake tool fills those for embedded textures afterward.
    std::vector<PackageTexture> Finalize() const;

    // Resolve an FBX texture path (UTF-8) that may be broken or authored on another machine: try the
    // given absolute path first, then each search directory with the filename appended, first match
    // wins. Returns an empty string when nothing resolves to an existing file.
    static std::string ResolveFbxTexturePath(const std::string& absPathFromFbx,
                                             const std::vector<std::string>& textureSearchDirs);

private:
    struct Entry
    {
        std::string name;       // TXTR Name (empty for FBX-sourced)
        std::string pkgRelPath; // TXTR Path, relative to the output .cpkg directory
    };

    // Normalize an absolute UTF-8 path into the dedup key (lexically normalized, forward slashes).
    static std::string KeyOf(const std::string& absPath);

    std::vector<Entry>                     m_Entries;
    std::unordered_map<std::string, int32_t> m_IndexByKey;
};

} // namespace Canvas::Cpkg
