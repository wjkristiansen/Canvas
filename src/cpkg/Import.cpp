//================================================================================================
// Import - the part of the Expand stage that builds imports, whatever their exchange format
//
// See Import.h. Each exchange format's reader is in its own file, such as FbxImport.cpp.
//================================================================================================

#include "Import.h"

namespace CpkgTool
{

namespace
{

// Per SourceImportKind: the noun a diagnostic names the file's content of the kind by, the
// default name template, the name of unnamed content, before its position, and the kind's key
// in "names" and "rename".
constexpr const char *kKindNouns[] = { "texture", "material", "mesh", "skeleton", "skin", "skeletal animation", "clip", "node" };
constexpr const char *kDefaultTemplates[] = { "{name}.image", "{name}.material", "{name}.mesh", "{name}.skeleton", "{name}.skin", "{name}.anim", "{name}", "{name}" };
constexpr const char *kUnnamed[] = { "Image", "Material", "Mesh", "Skeleton", "Skin", "SkeletalAnimation", "Clip", "Node" };
constexpr const char *kKindKeys[] = { "images", "materials", "meshes", "skeletons", "skins", "skeletalAnimations", "clips", "nodes" };
static_assert(std::size(kKindNouns) == size_t(SourceImportKind::Count));
static_assert(std::size(kDefaultTemplates) == size_t(SourceImportKind::Count));
static_assert(std::size(kUnnamed) == size_t(SourceImportKind::Count));
static_assert(std::size(kKindKeys) == size_t(SourceImportKind::Count));

// The kinds an import builds; "rename" of any later kind is reported as one cpkg cannot build yet.
constexpr size_t kBuiltKinds = size_t(SourceImportKind::Skeletons);

// Whether name can name an asset: CanvasPkg's name rules, and no leading '$'.
bool IsAssetName(std::string_view name)
{
    return !name.empty() && name[0] != '$' && Canvas::CpkgIsValidName(name);
}

// Why name, which IsAssetName rejects, is not a name.
std::string NotANameReason(std::string_view name)
{
    if (name.empty())
        return "it is empty";
    if (name[0] == '$')
        return "it begins with '$'";
    if (name.find('/') != std::string_view::npos)
        return "it holds a '/'";
    if (name.find('\0') != std::string_view::npos)
        return "it holds a NUL";
    return "it is not valid UTF-8";
}

Vec3 Rotate(const Vec4 &q, const Vec3 &v)
{
    // v + 2w(u x v) + 2u x (u x v), for q = (u, w)
    const Vec3 u = { q[0], q[1], q[2] };
    auto cross = [](const Vec3 &a, const Vec3 &b) -> Vec3
    {
        return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    };
    const Vec3 uv = cross(u, v);
    const Vec3 uuv = cross(u, uv);
    return { v[0] + 2.0f * (q[3] * uv[0] + uuv[0]), v[1] + 2.0f * (q[3] * uv[1] + uuv[1]), v[2] + 2.0f * (q[3] * uv[2] + uuv[2]) };
}

Vec3 Normalized(const Vec3 &v)
{
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return length > 0.0f ? Vec3{ v[0] / length, v[1] / length, v[2] / length } : v;
}

// The extension of path, in lower case, such as ".fbx".
std::string LowerExtension(const std::string &path)
{
    std::string extension = PathOf(path).extension().string();
    for (char &c : extension)
    {
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    }
    return extension;
}

} // namespace

//------------------------------------------------------------------------------------------------
CImportBuilder::CImportBuilder(SourcePackage &package, SourceBundle &bundle, const SourceImport &import, CDiagnostics &diagnostics) :
    m_Package(package),
    m_Bundle(bundle),
    m_Import(import),
    m_Diagnostics(diagnostics)
{
    for (size_t kind = 0; kind < size_t(SourceImportKind::Count); ++kind)
        m_RenameUsed[kind].assign(import.Rename[kind].size(), false);
    m_ReplaceImagesUsed.assign(import.ReplaceImages.size(), false);
    m_ReplaceMaterialsUsed.assign(import.ReplaceMaterials.size(), false);
    if (import.Rotation)
        m_Rotation = import.Rotation->Quaternion;

    // What the import declares that cpkg cannot build yet
    if (import.Nodes)
        Unsupported(import.Where, "an import's nodes");
    if (import.Parent)
        Unsupported(import.Parent->Where, "an import's \"parent\"");
    if (import.Skeleton)
        Unsupported(import.Skeleton->Where, "an import's \"skeleton\"");
    if (!import.Edits.empty())
        Unsupported(import.Edits[0].Name.Where, "an import's \"edits\"");
    if (import.Clips && !import.Clips->empty())
        Unsupported((*import.Clips)[0].Where, "an import's \"clips\"");
    for (size_t kind = kBuiltKinds; kind < size_t(SourceImportKind::Count); ++kind)
    {
        if (!import.Rename[kind].empty())
            Unsupported(import.Rename[kind][0].first.Where, std::string("an import's \"rename\" of \"") + kKindKeys[kind] + "\"");
    }
}

void CImportBuilder::Error(const SourceLocation &where, const std::string &message)
{
    ReportAt(m_Diagnostics, m_Package, Severity::Error, where, message);
}

void CImportBuilder::Warning(const SourceLocation &where, const std::string &message)
{
    ReportAt(m_Diagnostics, m_Package, Severity::Warning, where, message);
}

void CImportBuilder::Unsupported(const SourceLocation &where, const std::string &what)
{
    m_Unsupported.emplace_back(where, "cpkg cannot build " + what + " yet");
}

std::string CImportBuilder::FileName(SourceImportKind kind, std::string_view name, size_t index)
{
    return name.empty() ? kUnnamed[size_t(kind)] + std::to_string(index) : std::string(name);
}

std::string CImportBuilder::Describe(SourceImportKind kind, std::string_view name) const
{
    return std::string("the ") + m_pFormat + " " + kKindNouns[size_t(kind)] + " " + Quoted(name);
}

SourceString CImportBuilder::BuiltName(SourceImportKind kind, const std::string &fileName)
{
    if (const SourceString *pRenamed = Find(m_Import.Rename[size_t(kind)], m_RenameUsed[size_t(kind)], fileName))
        return *pRenamed;

    SourceString built;
    built.Where = m_Import.Where;
    const std::optional<SourceString> &pattern = m_Import.Names[size_t(kind)];
    const std::string_view text = pattern ? std::string_view(pattern->Text) : std::string_view(kDefaultTemplates[size_t(kind)]);
    constexpr std::string_view kField = "{name}";
    for (size_t start = 0;;)
    {
        const size_t field = text.find(kField, start);
        built.Text += text.substr(start, field - start);
        if (field == std::string_view::npos)
            break;
        built.Text += fileName;
        start = field + kField.size();
    }

    if (!IsAssetName(built.Text))
    {
        Error(built.Where, Describe(kind, fileName) + " builds the name " + Quoted(built.Text) + ", and "
            + NotANameReason(built.Text) + "; name it with \"rename\"");
    }
    return built;
}

const SourceString *CImportBuilder::ReplacementImage(std::string_view name)
{
    return Find(m_Import.ReplaceImages, m_ReplaceImagesUsed, name);
}

const SourceString *CImportBuilder::ReplacementMaterial(std::string_view name)
{
    return Find(m_Import.ReplaceMaterials, m_ReplaceMaterialsUsed, name);
}

Vec3 CImportBuilder::Position(const Vec3 &position) const
{
    const Vec3 rotated = Rotate(m_Rotation, position);
    return { rotated[0] * m_Import.Scale, rotated[1] * m_Import.Scale, rotated[2] * m_Import.Scale };
}

Vec3 CImportBuilder::Direction(const Vec3 &direction) const
{
    return Normalized(Rotate(m_Rotation, direction));
}

void CImportBuilder::Finish(bool read)
{
    std::stable_sort(m_Unsupported.begin(), m_Unsupported.end(), [](const auto &a, const auto &b) { return a.first.Offset < b.first.Offset; });
    for (const auto &[where, message] : m_Unsupported)
        Error(where, message);
    if (!read)
        return;

    auto check = [&](const SourceStringMap &map, const std::vector<bool> &used, const char *noun)
    {
        for (size_t i = 0; i < map.size(); ++i)
        {
            if (!used[i])
                Error(map[i].first.Where, std::string("the ") + m_pFormat + " file has no " + noun + " " + Quoted(map[i].first.Text));
        }
    };
    for (size_t kind = 0; kind < kBuiltKinds; ++kind)
        check(m_Import.Rename[kind], m_RenameUsed[kind], kKindNouns[kind]);
    check(m_Import.ReplaceImages, m_ReplaceImagesUsed, kKindNouns[size_t(SourceImportKind::Images)]);
    check(m_Import.ReplaceMaterials, m_ReplaceMaterialsUsed, kKindNouns[size_t(SourceImportKind::Materials)]);
}

const SourceString *CImportBuilder::Find(const SourceStringMap &map, std::vector<bool> &used, std::string_view name)
{
    for (size_t i = 0; i < map.size(); ++i)
    {
        if (map[i].first.Text == name)
        {
            used[i] = true;
            return &map[i].second;
        }
    }
    return nullptr;
}

//------------------------------------------------------------------------------------------------
bool ExpandImports(SourcePackage &package, CDiagnostics &diagnostics)
{
    const size_t errorCount = diagnostics.GetErrorCount();
    for (SourceBundle &bundle : package.Bundles)
    {
        for (const SourceImport &import : bundle.Imports)
        {
            CImportBuilder builder(package, bundle, import, diagnostics);
            bool read = false;
            if (LowerExtension(import.File.Path) == ".fbx")
                read = ReadFbxImport(builder);
            else
                builder.Error(import.File.Where, "expected an exchange file in a format cpkg reads, \".fbx\": " + Quoted(import.File.Written));
            builder.Finish(read);
        }
    }
    return diagnostics.GetErrorCount() == errorCount;
}

} // namespace CpkgTool
