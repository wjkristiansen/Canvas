//================================================================================================
// Stages - the build's stages after Load, and what they share
//
// See "Pipeline" in src/cpkg/README.md. Each stage takes the SourcePackage Load produced.
//================================================================================================

#pragma once

#include "Source.h"

#include <string>
#include <string_view>
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

private:
    const SourcePackage &m_Package;
};

// The Validate stage: checks the validation rules of PackageSource.md that Load does not, and
// reports each finding at the value that breaks the rule, in document order within each file.
// Returns true when it reported no error.
bool ValidatePackageSource(const SourcePackage &package, CDiagnostics &diagnostics);

// The Write stage: encodes each asset's payload and writes the package to path, a UTF-8 file
// path, replacing any file there. Expects a package that passed Validate. Returns true when the
// package was written.
bool WritePackage(const SourcePackage &package, const std::string &path, CDiagnostics &diagnostics);

} // namespace CpkgTool
