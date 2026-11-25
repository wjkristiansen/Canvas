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

// Per SourceImportKind: whether an import builds it; "rename" of any other kind is reported as
// one cpkg cannot build yet.
constexpr bool kBuiltKinds[] = { true, true, true, false, false, false, false, true };
static_assert(std::size(kBuiltKinds) == size_t(SourceImportKind::Count));

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

// The rotation a then b: the quaternion product b * a.
Vec4 Then(const Vec4 &a, const Vec4 &b)
{
    const Vec4 q = {
        b[3] * a[0] + b[0] * a[3] + b[1] * a[2] - b[2] * a[1],
        b[3] * a[1] - b[0] * a[2] + b[1] * a[3] + b[2] * a[0],
        b[3] * a[2] + b[0] * a[1] - b[1] * a[0] + b[2] * a[3],
        b[3] * a[3] - b[0] * a[0] - b[1] * a[1] - b[2] * a[2],
    };
    const float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    return { q[0] / length, q[1] / length, q[2] / length, q[3] / length };
}

// The node of nodes or their descendants named name, or null.
SourceNode *FindNode(std::vector<SourceNode> &nodes, std::string_view name)
{
    for (SourceNode &node : nodes)
    {
        if (node.Name && node.Name->Text == name)
            return &node;
        if (SourceNode *pFound = FindNode(node.Attachments.Children, name))
            return pFound;
    }
    return nullptr;
}

template <class T>
void SetIf(std::optional<T> &target, const std::optional<T> &value)
{
    if (value)
        target = value;
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
    m_EditsUsed.assign(import.Edits.size(), false);
    if (import.Rotation)
        m_Rotation = import.Rotation->Quaternion;

    // What applies only to the nodes of an import that builds none
    if (!import.Nodes)
    {
        constexpr const char *kNoNodes = " applies to the nodes the import builds, and it has \"nodes\": false";
        if (import.Parent)
            Error(import.Parent->Where, std::string("\"parent\"") + kNoNodes);
        for (const auto &[name, renamed] : import.Rename[size_t(SourceImportKind::Nodes)])
            Error(name.Where, std::string("\"rename\" of \"nodes\"") + kNoNodes);
        for (const SourceNamed<SourceNodeEdit> &edit : import.Edits)
            Error(edit.Name.Where, std::string("\"edits\"") + kNoNodes);
    }

    // What the import declares that cpkg cannot build yet
    if (import.Skeleton)
        Unsupported(import.Skeleton->Where, "an import's \"skeleton\"");
    if (import.Clips && !import.Clips->empty())
        Unsupported((*import.Clips)[0].Where, "an import's \"clips\"");
    for (size_t kind = 0; kind < size_t(SourceImportKind::Count); ++kind)
    {
        if (!kBuiltKinds[kind] && !import.Rename[kind].empty())
            Unsupported(import.Rename[kind][0].first.Where, std::string("an import's \"rename\" of \"") + kKindKeys[kind] + "\"");
    }
}

void CImportBuilder::Error(const SourceLocation &where, const std::string &message)
{
    m_Findings.push_back({ Severity::Error, where, message });
}

void CImportBuilder::Warning(const SourceLocation &where, const std::string &message)
{
    m_Findings.push_back({ Severity::Warning, where, message });
}

void CImportBuilder::Unsupported(const SourceLocation &where, const std::string &what)
{
    Error(where, "cpkg cannot build " + what + " yet");
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

SourceTransform CImportBuilder::RootTransform(const Vec3 &translation, const Vec4 &rotation, const Vec3 &scale) const
{
    SourceTransform transform;
    transform.Translation = Position(translation);
    SourceRotation &rotated = transform.Rotation.emplace();
    rotated.Quaternion = Then(rotation, m_Rotation);
    rotated.Where = m_Import.Where;
    transform.Scale = scale;
    return transform;
}

void CImportBuilder::AddNodes(std::vector<SourceNode> roots)
{
    for (SourceNode &root : roots)
        ApplyEdits(root);

    std::vector<SourceNode> *pSiblings = &m_Bundle.Nodes;
    if (m_Import.Parent)
    {
        SourceNode *pParent = FindNode(m_Bundle.Nodes, m_Import.Parent->Text);
        for (size_t i = 0; !pParent && i < m_Bundle.ImportNodes.size(); ++i)
        {
            SourceImportNode &importNode = m_Bundle.ImportNodes[i];
            pParent = FindNode(importNode.Attachments.Children, m_Import.Parent->Text);
        }
        if (!pParent)
        {
            Error(m_Import.Parent->Where, "the bundle has no node " + Quoted(m_Import.Parent->Text));
            return;
        }
        pSiblings = &pParent->Attachments.Children;
    }
    for (SourceNode &root : roots)
        pSiblings->push_back(std::move(root));
}

void CImportBuilder::ApplyEdits(SourceNode &node)
{
    // The children an edit adds are authored content, which no edit applies to.
    const size_t built = node.Attachments.Children.size();
    for (size_t i = 0; node.Name && i < m_Import.Edits.size(); ++i)
    {
        const SourceNamed<SourceNodeEdit> &edit = m_Import.Edits[i];
        if (edit.Name.Text != node.Name->Text)
            continue;
        m_EditsUsed[i] = true;
        const SourceNodeEdit &value = edit.Value;
        SetIf(node.Transform.Translation, value.Transform.Translation);
        SetIf(node.Transform.Rotation, value.Transform.Rotation);
        SetIf(node.Transform.Scale, value.Transform.Scale);
        SetIf(node.Mobility, value.Mobility);

        SourceAttachments &attachments = node.Attachments;
        const SourceAttachments &added = value.Attachments;
        if (added.Camera)
        {
            if (!attachments.Camera)
                attachments.Camera = added.Camera;
            else
            {
                SourceCamera &camera = *attachments.Camera;
                SetIf(camera.NearClip, added.Camera->NearClip);
                SetIf(camera.FarClip, added.Camera->FarClip);
                SetIf(camera.FovAngle, added.Camera->FovAngle);
                SetIf(camera.ExposureStops, added.Camera->ExposureStops);
            }
        }
        if (added.Light)
        {
            if (!attachments.Light)
            {
                if (!added.Light->Type)
                    Error(added.Light->Where, "the node " + Quoted(node.Name->Text) + " has no light, so the edit's light needs a \"type\"");
                attachments.Light = added.Light;
            }
            else
            {
                SourceLight &light = *attachments.Light;
                const SourceLight &set = *added.Light;
                SetIf(light.Type, set.Type);
                SetIf(light.Color, set.Color);
                SetIf(light.Intensity, set.Intensity);
                SetIf(light.Enabled, set.Enabled);
                SetIf(light.CastsShadows, set.CastsShadows);
                SetIf(light.Range, set.Range);
                SetIf(light.AttenuationConstant, set.AttenuationConstant);
                SetIf(light.AttenuationLinear, set.AttenuationLinear);
                SetIf(light.AttenuationQuadratic, set.AttenuationQuadratic);
                SetIf(light.SpotInnerAngle, set.SpotInnerAngle);
                SetIf(light.SpotOuterAngle, set.SpotOuterAngle);
                SetIf(light.ShadowResolution, set.ShadowResolution);
                SetIf(light.ShadowConstantBias, set.ShadowConstantBias);
                SetIf(light.ShadowSlopeScaleBias, set.ShadowSlopeScaleBias);
                SetIf(light.ShadowNormalOffset, set.ShadowNormalOffset);
            }
        }
        attachments.MeshInstances.insert(attachments.MeshInstances.end(), added.MeshInstances.begin(), added.MeshInstances.end());
        attachments.MeshFields.insert(attachments.MeshFields.end(), added.MeshFields.begin(), added.MeshFields.end());
        attachments.Placements.insert(attachments.Placements.end(), added.Placements.begin(), added.Placements.end());
        attachments.Children.insert(attachments.Children.end(), added.Children.begin(), added.Children.end());
    }

    for (size_t i = 0; i < built; ++i)
        ApplyEdits(node.Attachments.Children[i]);
}

void CImportBuilder::Finish(bool read)
{
    if (read)
    {
        auto check = [&](const SourceStringMap &map, const std::vector<bool> &used, const char *noun)
        {
            for (size_t i = 0; i < map.size(); ++i)
            {
                if (!used[i])
                    Error(map[i].first.Where, std::string("the ") + m_pFormat + " file has no " + noun + " " + Quoted(map[i].first.Text));
            }
        };
        for (size_t kind = 0; kind < size_t(SourceImportKind::Count); ++kind)
        {
            if (kBuiltKinds[kind] && (kind != size_t(SourceImportKind::Nodes) || m_Import.Nodes))
                check(m_Import.Rename[kind], m_RenameUsed[kind], kKindNouns[kind]);
        }
        check(m_Import.ReplaceImages, m_ReplaceImagesUsed, kKindNouns[size_t(SourceImportKind::Images)]);
        check(m_Import.ReplaceMaterials, m_ReplaceMaterialsUsed, kKindNouns[size_t(SourceImportKind::Materials)]);
        for (size_t i = 0; m_Import.Nodes && i < m_Import.Edits.size(); ++i)
        {
            if (!m_EditsUsed[i])
                Error(m_Import.Edits[i].Name.Where, "the import builds no node " + Quoted(m_Import.Edits[i].Name.Text));
        }
    }

    std::stable_sort(m_Findings.begin(), m_Findings.end(), [](const Finding &a, const Finding &b) { return a.Where.Offset < b.Where.Offset; });
    for (const Finding &finding : m_Findings)
        ReportAt(m_Diagnostics, m_Package, finding.Level, finding.Where, finding.Message);
    m_Findings.clear();
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
