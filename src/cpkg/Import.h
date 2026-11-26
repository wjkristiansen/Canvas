//================================================================================================
// Import - what an import builds, whatever the exchange format of its file
//
// ExpandImports (Stages.h) gives each import a CImportBuilder and runs the reader for its
// file's exchange format, which reads the file and adds the content it holds to the bundle
// through the builder. The builder applies what the import declares the same way for every
// format: names, "rename", "replace", "rotation", "scale", "parent", and "edits". See "Imports" in
// src/cpkg/README.md and in PackageSource.md.
//================================================================================================

#pragma once

#include "Stages.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CpkgTool
{

// Builds one import's content into the bundle that lists it.
class CImportBuilder
{
public:
    CImportBuilder(SourcePackage &package, SourceBundle &bundle, const SourceImport &import, CDiagnostics &diagnostics);

    SourcePackage &GetPackage() { return m_Package; }
    SourceBundle &GetBundle() { return m_Bundle; }
    const SourceImport &GetImport() const { return m_Import; }

    // Sets the name of the file's exchange format, such as "FBX", by which diagnostics name the
    // file and its content.
    void SetFormat(const char *pFormat) { m_pFormat = pFormat; }

    // Records a finding at where, which Finish reports, with the import's others, in document order.
    void Error(const SourceLocation &where, const std::string &message);
    void Warning(const SourceLocation &where, const std::string &message);

    // Records, as an error, content that the file holds, or that the import declares, that cpkg
    // cannot build yet.
    void Unsupported(const SourceLocation &where, const std::string &what);

    // The name of content of kind in the file: name, or for unnamed content, the name its
    // position among the file's content of that kind gives it, such as "Mesh0".
    static std::string FileName(SourceImportKind kind, std::string_view name, size_t index);

    // How a diagnostic names content of kind named name in the file, such as the FBX mesh "Crate".
    std::string Describe(SourceImportKind kind, std::string_view name) const;

    // The built name of the content of kind that FileName names: its "rename" entry, or else its
    // kind's name template applied to it, and for a skeletal animation, to the name of its
    // skeleton in the file. Reports a built name that is not a name.
    SourceString BuiltName(SourceImportKind kind, const std::string &fileName, std::string_view skeleton = {});

    // The asset path "replace" gives the image or material named name in the file, or null.
    const SourceString *ReplacementImage(std::string_view name);
    const SourceString *ReplacementMaterial(std::string_view name);

    // A position, direction, or rotation in Canvas space, with the import's "rotation" applied,
    // and for a position, its "scale". A direction comes out normalized. A rotation, such as a
    // bone's local rotation, changes basis to the space "rotation" turns, R * q * R^-1, so it
    // turns that space's axes as q turned Canvas's.
    Canvas::Math::FloatVector3 Position(const Canvas::Math::FloatVector3 &position) const;
    Canvas::Math::FloatVector3 Direction(const Canvas::Math::FloatVector3 &direction) const;
    Canvas::Math::FloatQuaternion Rotation(const Canvas::Math::FloatQuaternion &rotation) const;

    // A root node's rotation in Canvas space, with the import's "rotation" applied about the
    // file's origin after it: R * q.
    Canvas::Math::FloatQuaternion RootRotation(const Canvas::Math::FloatQuaternion &rotation) const;

    // Adds the nodes the import builds, roots and their descendants: applies "edits" to them,
    // and attaches the roots to the node "parent" names, or else to the bundle's root nodes.
    void AddNodes(std::vector<SourceNode> roots);

    // Reports the import's findings in document order: those recorded, and, when the reader
    // read the file, each key of "rename" and "replace" that names no content of the file, and
    // each key of "edits" that names no node the import builds.
    void Finish(bool read);

private:
    static const SourceString *Find(const SourceStringMap &map, std::vector<bool> &used, std::string_view name);
    void ApplyEdits(SourceNode &node);

    SourcePackage &m_Package;
    SourceBundle &m_Bundle;
    const SourceImport &m_Import;
    CDiagnostics &m_Diagnostics;
    const char *m_pFormat = "";
    Canvas::Math::FloatQuaternion m_Rotation;

    std::array<std::vector<bool>, size_t(SourceImportKind::Count)> m_RenameUsed;
    std::vector<bool> m_ReplaceImagesUsed;
    std::vector<bool> m_ReplaceMaterialsUsed;
    std::vector<bool> m_EditsUsed;
    struct Finding
    {
        Severity Level;
        SourceLocation Where;
        std::string Message;
    };
    std::vector<Finding> m_Findings;    // Finish reports each, in document order
};

// The reader of FBX files: reads the import's file and builds its content. Returns true when it
// read the file.
bool ReadFbxImport(CImportBuilder &builder);

} // namespace CpkgTool
