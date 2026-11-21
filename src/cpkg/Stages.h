//================================================================================================
// Stages - the build's stages after Load, and what they share
//
// See "Pipeline" in src/cpkg/README.md. Each stage takes the SourcePackage Load produced.
//================================================================================================

#pragma once

#include "Source.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace CpkgTool
{

// What an asset path within a package source resolves to.
struct AssetTarget
{
    enum class Kind
    {
        Asset,          // an asset the package source declares; pBundle, Type, and Index say which
        OtherPackage,   // in another package
        ByImport,       // not declared in pBundle, whose imports may build it
        NotFound,       // in pBundle, or with a null pBundle in a bundle the package lacks
    };

    Kind State = Kind::NotFound;
    const SourceBundle *pBundle = nullptr;
    Canvas::AssetType Type = Canvas::AssetType::Unknown;
    size_t Index = 0;       // in the bundle's member of that type, such as SourceBundle::Skeletons
};

// The names of a well-formed path, in order.
std::vector<std::string_view> PathNames(std::string_view path);

// Resolves asset paths, written relative to a bundle of the package, to the shared assets the
// package source declares.
class CPackageLookup
{
public:
    explicit CPackageLookup(const SourcePackage &package) :
        m_Package(package)
    {
    }

    // Resolves path, a well-formed asset path written in bundle from.
    AssetTarget ResolveAssetPath(const SourceBundle &from, std::string_view path) const;

    // The bundle path, a well-formed bundle path, names within the package, or null. Sets
    // *pOtherPackage to whether path names a bundle of another package.
    const SourceBundle *ResolveBundlePath(std::string_view path, bool *pOtherPackage) const;

private:
    const SourceBundle *FindBundle(std::string_view name) const;

    const SourcePackage &m_Package;
};

// One local asset of a bundle, at its place in the bundle's append order.
struct LocalAsset
{
    using Source = std::variant<const SourceNode *, const SourceImportNode *, const SourcePlacement *,
        const SourceSkeletonInstance *, const SourceMeshInstance *, const SourceMeshField *, const SourceCamera *,
        const SourceLight *, const SourceClip *, const std::vector<SourceString> *, const SourceWorld *>;

    Canvas::AssetType Type = Canvas::AssetType::Unknown;
    Source pSource;                         // the source it builds from; for an ExportList, the bundle's exports
    const SourceString *pName = nullptr;    // null when unnamed
    int32_t Index = -1;                     // its asset index

    // The asset index of the asset it is relative to or attaches to: a TransformNode's
    // ParentIndex, an ImportNode's PlacementIndex, or the NodeIndex of any other. -1 for none.
    int32_t Reference = -1;

    // For a node, the SkeletonInstance attached to it; for a mesh instance, the one it binds.
    // -1 for none.
    int32_t SkeletonInstance = -1;

    // For a node, its parent node in the source: the node whose children or the import node whose
    // children hold it. -1 for a root node or an import node.
    int32_t TreeParent = -1;
};

// The Resolve stage for one bundle's local assets: places them in CanvasPkg's canonical order, as
// PackageSource.md defines it, and resolves the names they refer to each other by. Validate uses
// it to check references by name, and Write to encode them. Every reference that resolves is
// resolved, so Validate can report each one that does not, and Write can rely on the result for
// a bundle that passed Validate:
// - A node with "bone" refers to its tree parent's skeleton instance; -1 when it has none.
// - An import node's placement is the placement of that name, wherever it is. An import node
//   whose placement is not found, or attaches within the import node, follows the other
//   hierarchy assets, with Reference -1 or a later index.
// - A mesh instance with a skin binds the skeleton instance of the node its "skeletonInstance"
//   names, or else of the nearest node with one from its own node toward its root node.
class CBundleLayout
{
public:
    explicit CBundleLayout(const SourceBundle &bundle);

    const SourceBundle &GetBundle() const { return m_Bundle; }

    // The bundle's local assets, in append order, after its shared assets.
    const std::vector<LocalAsset> &GetLocalAssets() const { return m_Assets; }

    // The local asset at asset index index.
    const LocalAsset &GetAsset(int32_t index) const { return m_Assets[size_t(index - m_FirstIndex)]; }

    // The first local asset named name, or null.
    const LocalAsset *FindNamed(std::string_view name) const;

    // The asset index of the Camera attached to the node at asset index node, or -1.
    int32_t CameraOf(int32_t node) const;

    // True for TransformNode and ImportNode.
    static bool IsNode(Canvas::AssetType type);

private:
    struct Owner
    {
        int32_t Node;                           // asset index; -1 for the bundle's own
        const SourceAttachments *pAttachments;
    };

    LocalAsset &At(int32_t index) { return m_Assets[size_t(index - m_FirstIndex)]; }
    LocalAsset &Add(Canvas::AssetType type, LocalAsset::Source pSource, const SourceString *pName, int32_t reference);
    LocalAsset &Add(Canvas::AssetType type, LocalAsset::Source pSource, const std::optional<SourceString> &name, int32_t reference);
    void AddNode(const SourceNode &node, int32_t treeParent);
    void AddAttachments(int32_t node, const SourceAttachments &attachments);
    void AddPlacement(const SourcePlacement &placement, int32_t node);
    void AddImportNode(size_t importIndex, int32_t placement);
    int32_t BindSkeletonInstance(const SourceMeshInstance &instance, int32_t node) const;

    const SourceBundle &m_Bundle;
    int32_t m_FirstIndex = 0;
    std::vector<LocalAsset> m_Assets;
    std::vector<Owner> m_Owners;            // in append order
    std::vector<bool> m_ImportNodeAdded;    // per entry of the bundle's importNodes
    std::unordered_map<std::string_view, int32_t> m_Names;
};

// The Validate stage: checks the validation rules of PackageSource.md that Load does not, and
// reports each finding at the value that breaks the rule, in document order within each file.
// Returns true when it reported no error.
bool ValidatePackageSource(const SourcePackage &package, CDiagnostics &diagnostics);

// The Write stage: encodes each asset's payload and writes the package to path, a UTF-8 file
// path. It writes a temporary file beside path and replaces path with it only once the whole
// package is written, and leaves path untouched when it already holds the same bytes. Expects a
// package that passed Validate. Returns true when path holds the package.
bool WritePackage(const SourcePackage &package, const std::string &path, CDiagnostics &diagnostics);

//------------------------------------------------------------------------------------------------
// Files and packages

// Whether the files at a and b both exist and hold the same bytes.
bool SameBytes(const std::filesystem::path &a, const std::filesystem::path &b);

// A temporary file or folder name beside path, a UTF-8 path, unique to this process.
std::string TemporaryPath(const std::string &path);

// The enumerator's name, such as "TransformNode", or null for a value with no name.
const char *AssetTypeName(Canvas::AssetType type);

// Why CCpkgReader::Open failed with result for the .cpkg file at package, a UTF-8 path.
std::string OpenFailure(Gem::Result result, const std::string &package);

//------------------------------------------------------------------------------------------------
// The standard form

// The shortest JSON number that reads back as value, an integer when it is one. value is finite.
Json FloatJson(float value);

// The text of file in the standard form: its members equal to their defaults removed, its
// numbers respelled, and laid out by JsonFormat.
std::string StandardText(const SourceFile &file);

} // namespace CpkgTool
