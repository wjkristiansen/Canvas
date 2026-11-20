//================================================================================================
// BundleLayout - a bundle's local assets in append order, and the names they refer to
//================================================================================================

#include "Stages.h"

namespace CpkgTool
{

using Canvas::AssetType;

//------------------------------------------------------------------------------------------------
CBundleLayout::CBundleLayout(const SourceBundle &bundle) :
    m_Bundle(bundle),
    m_ImportNodeAdded(bundle.ImportNodes.size(), false)
{
    m_FirstIndex = int32_t(bundle.Images.size() + bundle.Skyboxes.size() + bundle.Materials.size() + bundle.Meshes.size()
        + bundle.Skeletons.size() + bundle.Skins.size() + bundle.SkeletalAnimations.size());

    // The hierarchy assets, in pre-order.
    for (const SourceNode &node : bundle.Nodes)
        AddNode(node, -1);
    for (const SourcePlacement &placement : bundle.Placements)
        AddPlacement(placement, -1);
    // An import node not added yet names no placement added before it: none, or one within its
    // own subtree. Once every name is known, the latter takes that placement's later index.
    for (size_t i = 0; i < bundle.ImportNodes.size(); ++i)
    {
        if (!m_ImportNodeAdded[i])
            AddImportNode(i, -1);
    }
    for (LocalAsset &asset : m_Assets)
    {
        if (asset.Type == AssetType::ImportNode && asset.Reference == -1)
        {
            const LocalAsset *pPlacement = FindNamed(std::get<const SourceImportNode *>(asset.pSource)->Placement.Text);
            if (pPlacement && pPlacement->Type == AssetType::Placement)
                asset.Reference = pPlacement->Index;
        }
    }

    // The attachments, each kind in the order of the nodes they attach to.
    for (const Owner &owner : m_Owners)
    {
        for (const SourceMeshInstance &instance : owner.pAttachments->MeshInstances)
        {
            const int32_t binding = BindSkeletonInstance(instance, owner.Node);
            Add(AssetType::MeshInstance, &instance, std::nullopt, owner.Node).SkeletonInstance = binding;
        }
    }
    for (const Owner &owner : m_Owners)
    {
        for (const SourceMeshField &field : owner.pAttachments->MeshFields)
            Add(AssetType::MeshField, &field, field.Name, owner.Node);
    }
    for (const SourceMeshField &field : bundle.MeshFields)
        Add(AssetType::MeshField, &field, field.Name, -1);
    for (const Owner &owner : m_Owners)
    {
        if (owner.pAttachments->Camera)
            Add(AssetType::Camera, &*owner.pAttachments->Camera, std::nullopt, owner.Node);
    }
    for (const Owner &owner : m_Owners)
    {
        if (owner.pAttachments->Light)
            Add(AssetType::Light, &*owner.pAttachments->Light, std::nullopt, owner.Node);
    }

    for (const SourceNamed<SourceClip> &clip : bundle.Clips)
        Add(AssetType::AnimationClip, &clip.Value, &clip.Name, -1);
    if (bundle.Exports)
        Add(AssetType::ExportList, &*bundle.Exports, std::nullopt, -1);
    if (bundle.World)
        Add(AssetType::WorldSettings, &*bundle.World, std::nullopt, -1);
}

const LocalAsset *CBundleLayout::FindNamed(std::string_view name) const
{
    const auto found = m_Names.find(name);
    return found == m_Names.end() ? nullptr : &GetAsset(found->second);
}

int32_t CBundleLayout::CameraOf(int32_t node) const
{
    for (const LocalAsset &asset : m_Assets)
    {
        if (asset.Type == AssetType::Camera && asset.Reference == node)
            return asset.Index;
    }
    return -1;
}

bool CBundleLayout::IsNode(AssetType type)
{
    return type == AssetType::TransformNode || type == AssetType::ImportNode;
}

//------------------------------------------------------------------------------------------------
LocalAsset &CBundleLayout::Add(AssetType type, LocalAsset::Source pSource, const SourceString *pName, int32_t reference)
{
    LocalAsset &asset = m_Assets.emplace_back();
    asset.Type = type;
    asset.pSource = pSource;
    asset.Index = m_FirstIndex + int32_t(m_Assets.size() - 1);
    asset.Reference = reference;
    if (pName)
    {
        asset.pName = pName;
        m_Names.emplace(pName->Text, asset.Index);
    }
    return asset;
}

LocalAsset &CBundleLayout::Add(AssetType type, LocalAsset::Source pSource, const std::optional<SourceString> &name, int32_t reference)
{
    return Add(type, pSource, name ? &*name : nullptr, reference);
}

// Adds a node, then what it holds: its skeleton instance, its placements, and its children.
void CBundleLayout::AddNode(const SourceNode &node, int32_t treeParent)
{
    int32_t parent = treeParent;
    if (node.Bone)
        parent = treeParent == -1 ? -1 : GetAsset(treeParent).SkeletonInstance;
    const int32_t index = Add(AssetType::TransformNode, &node, node.Name, parent).Index;
    At(index).TreeParent = treeParent;
    AddAttachments(index, node.Attachments);
}

void CBundleLayout::AddAttachments(int32_t node, const SourceAttachments &attachments)
{
    m_Owners.push_back({ node, &attachments });
    if (attachments.SkeletonInstance)
    {
        const int32_t instance = Add(AssetType::SkeletonInstance, &*attachments.SkeletonInstance, std::nullopt, node).Index;
        At(node).SkeletonInstance = instance;
    }
    for (const SourcePlacement &placement : attachments.Placements)
        AddPlacement(placement, node);
    for (const SourceNode &child : attachments.Children)
        AddNode(child, node);
}

// Adds a placement, then each import node that names it, in importNodes order.
void CBundleLayout::AddPlacement(const SourcePlacement &placement, int32_t node)
{
    const int32_t index = Add(AssetType::Placement, &placement, placement.Name, node).Index;
    if (!placement.Name)
        return;
    for (size_t i = 0; i < m_Bundle.ImportNodes.size(); ++i)
    {
        if (!m_ImportNodeAdded[i] && m_Bundle.ImportNodes[i].Placement.Text == placement.Name->Text)
            AddImportNode(i, index);
    }
}

void CBundleLayout::AddImportNode(size_t importIndex, int32_t placement)
{
    m_ImportNodeAdded[importIndex] = true;
    const SourceImportNode &importNode = m_Bundle.ImportNodes[importIndex];
    const int32_t index = Add(AssetType::ImportNode, &importNode, importNode.Name, placement).Index;
    AddAttachments(index, importNode.Attachments);
}

int32_t CBundleLayout::BindSkeletonInstance(const SourceMeshInstance &instance, int32_t node) const
{
    if (!instance.Skin)
        return -1;
    if (instance.SkeletonInstance)
    {
        const LocalAsset *pNode = FindNamed(instance.SkeletonInstance->Text);
        return pNode && IsNode(pNode->Type) ? pNode->SkeletonInstance : -1;
    }
    for (int32_t at = node; at != -1; at = GetAsset(at).TreeParent)
    {
        if (GetAsset(at).SkeletonInstance != -1)
            return GetAsset(at).SkeletonInstance;
    }
    return -1;
}

} // namespace CpkgTool
