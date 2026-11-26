//================================================================================================
// Import - the part of the Expand stage that builds imports, whatever their exchange format
//
// See Import.h. Each exchange format's reader is in its own file, such as FbxImport.cpp.
//================================================================================================

#include "Import.h"

namespace CpkgTool
{

namespace Math = Canvas::Math;

namespace
{

// What the build does with one kind of content an import builds.
struct KindInfo
{
    SourceImportKind Kind;
    const char *pNoun;              // what a diagnostic calls the file's content of the kind
    const char *pDefaultTemplate;   // its name template when "names" gives none
    const char *pUnnamed;           // the name of unnamed content, before its position
    bool NeedsNodes;                // whether an import builds it only when it builds nodes
};

// Indexed by kind.
constexpr KindInfo kKinds[] = {
    { SourceImportKind::Images,             "texture",              "{name}.image",     "Image",                false },
    { SourceImportKind::Materials,          "material",             "{name}.material",  "Material",             false },
    { SourceImportKind::Meshes,             "mesh",                 "{name}.mesh",      "Mesh",                 false },
    { SourceImportKind::Skeletons,          "skeleton",             "{name}.skeleton",  "Skeleton",             false },
    { SourceImportKind::Skins,              "skin",                 "{name}.skin",      "Skin",                 false },
    { SourceImportKind::SkeletalAnimations, "skeletal animation",   "{name}.anim",      "SkeletalAnimation",    false },
    { SourceImportKind::Clips,              "clip",                 "{name}",           "Clip",                 true },
    { SourceImportKind::Nodes,              "node",                 "{name}",           "Node",                 true },
};
static_assert(IsPerImportKind(kKinds));

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

    // What applies only to an import that builds nodes
    if (!import.Nodes)
    {
        constexpr const char *kNoNodes = " applies only to an import that builds nodes, and this one has \"nodes\": false";
        if (import.Parent)
            Error(import.Parent->Where, std::string("\"parent\"") + kNoNodes);
        for (size_t kind = 0; kind < size_t(SourceImportKind::Count); ++kind)
        {
            for (size_t i = 0; kKinds[kind].NeedsNodes && i < import.Rename[kind].size(); ++i)
                Error(import.Rename[kind][i].first.Where, std::string("\"rename\" of \"") + kImportKinds[kind].pKey + "\"" + kNoNodes);
        }
        for (const SourceNamed<SourceNodeEdit> &edit : import.Edits)
            Error(edit.Name.Where, std::string("\"edits\"") + kNoNodes);
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
    return name.empty() ? kKinds[size_t(kind)].pUnnamed + std::to_string(index) : std::string(name);
}

std::string CImportBuilder::Describe(SourceImportKind kind, std::string_view name) const
{
    return std::string("the ") + m_pFormat + " " + kKinds[size_t(kind)].pNoun + " " + Quoted(name);
}

SourceString CImportBuilder::BuiltName(SourceImportKind kind, const std::string &fileName, std::string_view skeleton)
{
    if (const SourceString *pRenamed = Find(m_Import.Rename[size_t(kind)], m_RenameUsed[size_t(kind)], fileName))
        return *pRenamed;

    SourceString built;
    built.Where = m_Import.Where;
    const std::optional<SourceString> &pattern = m_Import.Names[size_t(kind)];
    const std::string_view text = pattern ? std::string_view(pattern->Text) : std::string_view(kKinds[size_t(kind)].pDefaultTemplate);
    constexpr std::string_view kName = "{name}";
    constexpr std::string_view kSkeleton = "{skeleton}";
    const bool skeletal = kind == SourceImportKind::SkeletalAnimations;
    for (size_t start = 0;;)
    {
        const size_t name = text.find(kName, start);
        const size_t field = skeletal ? std::min(name, text.find(kSkeleton, start)) : name;
        built.Text += text.substr(start, field - start);
        if (field == std::string_view::npos)
            break;
        const bool isName = field == name;
        built.Text += isName ? std::string_view(fileName) : skeleton;
        start = field + (isName ? kName : kSkeleton).size();
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

Math::FloatVector3 CImportBuilder::Position(const Math::FloatVector3 &position) const
{
    return Math::RotateVectorByQuaternion(m_Rotation, position) * m_Import.Scale;
}

Math::FloatVector3 CImportBuilder::Direction(const Math::FloatVector3 &direction) const
{
    return Math::RotateVectorByQuaternion(m_Rotation, direction).Normalize();
}

Math::FloatQuaternion CImportBuilder::Rotation(const Math::FloatQuaternion &rotation) const
{
    return (m_Rotation * rotation * Math::Conjugate(m_Rotation)).Normalize();
}

Math::FloatQuaternion CImportBuilder::RootRotation(const Math::FloatQuaternion &rotation) const
{
    return (m_Rotation * rotation).Normalize();
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

void CImportBuilder::Animate(const SourceString &name)
{
    SourceNode *pNode = FindNode(m_Bundle.Nodes, name.Text);
    for (size_t i = 0; !pNode && i < m_Bundle.ImportNodes.size(); ++i)
        pNode = FindNode(m_Bundle.ImportNodes[i].Attachments.Children, name.Text);
    if (pNode && !pNode->Mobility)
        pNode->Mobility = Canvas::CpkgMobility::Movable;
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
            if (!kKinds[kind].NeedsNodes || m_Import.Nodes)
                check(m_Import.Rename[kind], m_RenameUsed[kind], kKinds[kind].pNoun);
        }
        check(m_Import.ReplaceImages, m_ReplaceImagesUsed, kKinds[size_t(SourceImportKind::Images)].pNoun);
        check(m_Import.ReplaceMaterials, m_ReplaceMaterialsUsed, kKinds[size_t(SourceImportKind::Materials)].pNoun);
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

    // Every import builds its content but its clips before any builds its clips, so a clip finds
    // a skeleton "skeleton" names that an import of a later bundle builds.
    struct Expansion
    {
        std::unique_ptr<CImportBuilder> pBuilder;
        std::unique_ptr<CImportReader> pReader;
    };
    std::vector<Expansion> expansions;
    for (SourceBundle &bundle : package.Bundles)
    {
        for (const SourceImport &import : bundle.Imports)
        {
            Expansion &expansion = expansions.emplace_back();
            expansion.pBuilder = std::make_unique<CImportBuilder>(package, bundle, import, diagnostics);
            if (LowerExtension(import.File.Path) == ".fbx")
                expansion.pReader = ReadFbxImport(*expansion.pBuilder);
            else
                expansion.pBuilder->Error(import.File.Where, "expected an exchange file in a format cpkg reads, \".fbx\": " + Quoted(import.File.Written));
        }
    }
    for (Expansion &expansion : expansions)
    {
        if (expansion.pReader)
            expansion.pReader->BuildClips();
        expansion.pBuilder->Finish(expansion.pReader != nullptr);
    }
    return diagnostics.GetErrorCount() == errorCount;
}

} // namespace CpkgTool
