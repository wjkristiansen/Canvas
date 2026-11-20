//================================================================================================
// SourceValidate - the build's Validate stage, and asset path resolution
//================================================================================================

#include "Stages.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace CpkgTool
{

using Canvas::AssetType;

namespace
{

// text in double quotes, for a diagnostic.
std::string Quoted(std::string_view text)
{
    return "\"" + std::string(text) + "\"";
}

// The asset type with its article, such as "an image", for a diagnostic.
const char *AssetTypeNoun(AssetType type)
{
    switch (type)
    {
    case AssetType::Image: return "an image";
    case AssetType::Skybox: return "a skybox";
    case AssetType::Material: return "a material";
    case AssetType::MeshData: return "a mesh";
    case AssetType::Skeleton: return "a skeleton";
    case AssetType::MeshSkin: return "a skin";
    case AssetType::SkeletalAnimation: return "a skeletal animation";
    case AssetType::TransformNode: return "a node";
    case AssetType::ImportNode: return "an import node";
    case AssetType::Placement: return "a placement";
    case AssetType::MeshField: return "a mesh field";
    default: return "an asset";
    }
}

// The property's name as the package source writes it, such as "rotation".
const char *PropertyName(Canvas::CpkgAnimationProperty property)
{
    switch (property)
    {
    case Canvas::CpkgAnimationProperty::Translation: return "translation";
    case Canvas::CpkgAnimationProperty::Rotation: return "rotation";
    case Canvas::CpkgAnimationProperty::Scale: return "scale";
    }
    return "";
}

// The shape an image reference requires.
enum class ImageShape
{
    Any,
    Texture2D,      // a Texture2D image with one array slice, for a material's textures
    Cube,           // a Cube image with 6 array slices, for a skybox
};

// The asset named name in assets, or null. Sets *pIndex to its index when found.
template <class T>
const SourceNamed<T> *FindNamed(const std::vector<SourceNamed<T>> &assets, std::string_view name, size_t *pIndex)
{
    for (size_t i = 0; i < assets.size(); ++i)
    {
        if (assets[i].Name.Text == name)
        {
            *pIndex = i;
            return &assets[i];
        }
    }
    return nullptr;
}

// count followed by the noun in the form count calls for, such as "1 vertex" or "3 vertices".
// plural defaults to singular followed by "s".
std::string Plural(size_t count, std::string_view singular, std::string_view plural = {})
{
    const std::string many = plural.empty() ? std::string(singular) + "s" : std::string(plural);
    return std::to_string(count) + " " + (count == 1 ? std::string(singular) : many);
}

// Checks one package source and reports its findings in document order.
class CValidator
{
public:
    explicit CValidator(const SourcePackage &package) :
        m_Package(package),
        m_Lookup(package)
    {
    }

    bool Run(CDiagnostics &diagnostics)
    {
        CheckBundleNames();
        for (const SourceBundle &bundle : m_Package.Bundles)
            m_Layouts.emplace_back(bundle);
        for (const CBundleLayout &layout : m_Layouts)
            m_Placed.push_back(PlacedBundles(layout));
        for (size_t i = 0; i < m_Package.Bundles.size(); ++i)
            CheckBundle(i);

        std::stable_sort(m_Findings.begin(), m_Findings.end(), [](const Finding &a, const Finding &b)
        {
            return a.Where.File != b.Where.File ? a.Where.File < b.Where.File : a.Where.Offset < b.Where.Offset;
        });
        for (const Finding &finding : m_Findings)
            ReportAt(diagnostics, m_Package, Severity::Error, finding.Where, finding.Message);
        return m_Findings.empty();
    }

private:
    struct Finding
    {
        SourceLocation Where;
        std::string Message;
    };

    // Records an error at where, to report in document order when Run finishes.
    void Error(const SourceLocation &where, std::string message)
    {
        m_Findings.push_back({ where, std::move(message) });
    }

    //--------------------------------------------------------------------------------------------
    // Names

    // Every bundle of the package has its own name; a later use of a name is reported.
    void CheckBundleNames()
    {
        std::vector<std::string_view> names;
        for (const SourceBundle &bundle : m_Package.Bundles)
        {
            if (std::find(names.begin(), names.end(), bundle.Name.Text) != names.end())
                Error(bundle.Name.Where, "the package already has a bundle named " + Quoted(bundle.Name.Text));
            else
                names.push_back(bundle.Name.Text);
        }
    }

    // Every named asset of a bundle shares one namespace; a later use of a name is reported.
    void CheckAssetNames(const SourceBundle &bundle, const CBundleLayout &layout)
    {
        std::vector<const SourceString *> names;
        auto add = [&](const auto &assets)
        {
            for (const auto &asset : assets)
                names.push_back(&asset.Name);
        };
        add(bundle.Images);
        add(bundle.Materials);
        add(bundle.Meshes);
        add(bundle.Skeletons);
        add(bundle.Skins);
        add(bundle.SkeletalAnimations);
        add(bundle.Skyboxes);
        add(bundle.Clips);
        for (const LocalAsset &asset : layout.GetLocalAssets())
        {
            if (asset.pName)
                names.push_back(asset.pName);
        }

        std::stable_sort(names.begin(), names.end(), [](const SourceString *pA, const SourceString *pB)
        {
            return pA->Where.Offset < pB->Where.Offset;
        });
        for (size_t i = 0; i < names.size(); ++i)
        {
            for (size_t j = 0; j < i; ++j)
            {
                if (names[j]->Text == names[i]->Text)
                {
                    Error(names[i]->Where, "the bundle already uses the name " + Quoted(names[i]->Text));
                    break;
                }
            }
        }
    }

    //--------------------------------------------------------------------------------------------
    // References

    // Checks that path, written in bundle, resolves to an asset of type, and for an image, of
    // shape. Returns what it resolves to.
    AssetTarget CheckAssetPath(const SourceBundle &bundle, const SourceString &path, AssetType type, ImageShape shape = ImageShape::Any)
    {
        const AssetTarget target = m_Lookup.ResolveAssetPath(bundle, path.Text);
        switch (target.State)
        {
        case AssetTarget::Kind::NotFound:
        {
            const std::vector<std::string_view> names = PathNames(path.Text);
            if (!target.pBundle)
                Error(path.Where, "the package has no bundle " + Quoted(names[names.size() - 2]));
            else if (target.pBundle == &bundle)
                Error(path.Where, "the bundle has no asset " + Quoted(names.back()));
            else
                Error(path.Where, "the bundle " + Quoted(target.pBundle->Name.Text) + " has no asset " + Quoted(names.back()));
            break;
        }

        case AssetTarget::Kind::Asset:
            if (target.Type != type)
            {
                Error(path.Where, Quoted(path.Text) + " is " + AssetTypeNoun(target.Type) + ", not " + AssetTypeNoun(type));
                return { AssetTarget::Kind::NotFound, target.pBundle };
            }
            if (type == AssetType::Image && !HasShape(target.pBundle->Images[target.Index].Value, shape))
            {
                Error(path.Where, Quoted(path.Text) + (shape == ImageShape::Cube
                    ? " is not a cube image with 6 array slices" : " is not a 2D image with one array slice"));
            }
            break;

        default:
            break;
        }
        return target;
    }

    // Whether image has the shape the reference requires.
    static bool HasShape(const SourceImage &image, ImageShape shape)
    {
        using Canvas::CpkgImageDimension;
        switch (shape)
        {
        case ImageShape::Texture2D:
            return image.Dimension == CpkgImageDimension::Texture2D && image.ArraySize == 1;
        case ImageShape::Cube:
            return image.Dimension == CpkgImageDimension::Cube && image.ArraySize == 6;
        default:
            return true;
        }
    }

    // The skeleton a skeleton path resolves to within the package, or null.
    const SourceSkeleton *CheckSkeletonPath(const SourceBundle &bundle, const SourceString &path)
    {
        const AssetTarget target = CheckAssetPath(bundle, path, AssetType::Skeleton);
        if (target.State != AssetTarget::Kind::Asset)
            return nullptr;
        return &target.pBundle->Skeletons[target.Index].Value;
    }

    // The skeleton path resolves to within the package, or null. Reports nothing.
    const SourceSkeleton *FindSkeleton(const SourceBundle &bundle, const SourceString &path) const
    {
        const AssetTarget target = m_Lookup.ResolveAssetPath(bundle, path.Text);
        if (target.State != AssetTarget::Kind::Asset || target.Type != AssetType::Skeleton)
            return nullptr;
        return &target.pBundle->Skeletons[target.Index].Value;
    }

    // Reports a bone name the skeleton lacks. A null pSkeleton, from a path that did not resolve
    // within the package, is not checked.
    void CheckBone(const SourceSkeleton *pSkeleton, const SourceString &skeletonPath, const SourceString &bone)
    {
        if (!pSkeleton)
            return;
        for (const SourceBone &candidate : pSkeleton->Bones)
        {
            if (candidate.Name.Text == bone.Text)
                return;
        }
        Error(bone.Where, "the skeleton " + Quoted(skeletonPath.Text) + " has no bone " + Quoted(bone.Text));
    }

    // A rotation quaternion is normalized when written, so it must be non-zero.
    void CheckRotation(const SourceRotation &rotation)
    {
        const Vec4 &q = rotation.Quaternion;
        if (q[0] == 0.0f && q[1] == 0.0f && q[2] == 0.0f && q[3] == 0.0f)
            Error(rotation.Where, "expected a non-zero quaternion");
    }

    void CheckTransform(const SourceTransform &transform)
    {
        if (transform.Rotation)
            CheckRotation(*transform.Rotation);
    }

    // Reports a name that does not name a node of the bundle. A name the bundle lacks is not
    // reported for a bundle with imports, which may build it. Returns the node, or null.
    const LocalAsset *CheckNodeName(const SourceBundle &bundle, const CBundleLayout &layout, const SourceString &name)
    {
        const LocalAsset *pNamed = layout.FindNamed(name.Text);
        if (!pNamed)
        {
            if (bundle.Imports.empty())
                Error(name.Where, "the bundle has no node " + Quoted(name.Text));
            return nullptr;
        }
        if (!CBundleLayout::IsNode(pNamed->Type))
        {
            Error(name.Where, Quoted(name.Text) + " is " + AssetTypeNoun(pNamed->Type) + ", not a node");
            return nullptr;
        }
        return pNamed;
    }

    //--------------------------------------------------------------------------------------------
    // Assets

    void CheckBundle(size_t bundleIndex)
    {
        const SourceBundle &bundle = m_Package.Bundles[bundleIndex];
        const CBundleLayout &layout = m_Layouts[bundleIndex];
        CheckAssetNames(bundle, layout);
        for (const auto &material : bundle.Materials)
            CheckMaterial(bundle, material.Value);
        for (const auto &mesh : bundle.Meshes)
            CheckMesh(bundle, mesh.Value);
        for (const auto &skeleton : bundle.Skeletons)
            CheckSkeleton(skeleton.Value);
        for (const auto &skin : bundle.Skins)
            CheckSkin(bundle, skin.Value);
        for (const auto &animation : bundle.SkeletalAnimations)
            CheckSkeletalAnimation(bundle, animation.Value);
        for (const auto &skybox : bundle.Skyboxes)
            CheckSkybox(bundle, skybox.Value);
        CheckLocalAssets(bundleIndex, layout);
    }

    void CheckMaterial(const SourceBundle &bundle, const SourceMaterial &material)
    {
        for (const std::optional<SourceString> &texture : material.Textures)
        {
            if (texture)
                CheckAssetPath(bundle, *texture, AssetType::Image, ImageShape::Texture2D);
        }
        if (material.Displacement)
            CheckAssetPath(bundle, material.Displacement->Map, AssetType::Image, ImageShape::Texture2D);
    }

    // A vertex stream, of components values per vertex, has one entry per vertex.
    template <class T>
    void CheckStreamCount(const std::optional<SourceArray<T>> &stream, size_t components, size_t vertexCount)
    {
        if (stream && stream->Values.size() / components != vertexCount)
        {
            Error(stream->Where, "expected " + Plural(vertexCount, "vertex", "vertices") + ", as \"positions\" holds; found "
                + std::to_string(stream->Values.size() / components));
        }
    }

    void CheckMesh(const SourceBundle &bundle, const SourceMesh &mesh)
    {
        bool skinned = false;
        for (const SourceMeshGroup &group : mesh.Groups)
            skinned = skinned || group.BoneIndices || group.BoneWeights;

        for (const SourceMeshGroup &group : mesh.Groups)
        {
            if (group.Material)
                CheckAssetPath(bundle, *group.Material, AssetType::Material);

            const size_t vertexCount = group.Positions.Values.size() / 3;
            if (vertexCount == 0)
                Error(group.Positions.Where, "expected at least one vertex");
            if (!group.Normals)
                Error(group.Where, "a group needs \"normals\"");
            if (!group.UV0 && mesh.Topology == Canvas::GfxPrimitiveTopology::PatchList4CP)
                Error(group.Where, "a group of a \"patchList4CP\" mesh needs \"uv0\"");
            if (skinned && !(group.BoneIndices && group.BoneWeights))
                Error(group.Where, "a group of a skinned mesh needs \"boneIndices\" and \"boneWeights\"");

            CheckStreamCount(group.Normals, 3, vertexCount);
            CheckStreamCount(group.UV0, 2, vertexCount);
            CheckStreamCount(group.Tangents, 4, vertexCount);
            CheckStreamCount(group.BoneIndices, 4, vertexCount);
            CheckStreamCount(group.BoneWeights, 4, vertexCount);
        }

        if (mesh.BoundsForm == SourceBoundsForm::Declared)
        {
            for (size_t axis = 0; axis < 3; ++axis)
            {
                if (!(mesh.Bounds.Min[axis] <= mesh.Bounds.Max[axis]))
                {
                    Error(mesh.Bounds.Where, "expected \"min\" <= \"max\" on every axis");
                    break;
                }
            }
        }
    }

    void CheckSkeleton(const SourceSkeleton &skeleton)
    {
        for (size_t i = 0; i < skeleton.Bones.size(); ++i)
        {
            const SourceBone &bone = skeleton.Bones[i];
            for (size_t j = 0; j < i; ++j)
            {
                if (skeleton.Bones[j].Name.Text == bone.Name.Text)
                {
                    Error(bone.Name.Where, "the skeleton already has a bone named " + Quoted(bone.Name.Text));
                    break;
                }
            }

            if (bone.Parent)
            {
                auto named = [&](const SourceBone &candidate) { return candidate.Name.Text == bone.Parent->Text; };
                if (std::none_of(skeleton.Bones.begin(), skeleton.Bones.begin() + i, named))
                {
                    if (std::any_of(skeleton.Bones.begin() + i, skeleton.Bones.end(), named))
                        Error(bone.Parent->Where, Quoted(bone.Parent->Text) + " is not an earlier bone");
                    else
                        Error(bone.Parent->Where, "the skeleton has no bone " + Quoted(bone.Parent->Text));
                }
            }

            if (bone.Rest.Rotation)
                CheckRotation(*bone.Rest.Rotation);
        }
    }

    void CheckSkin(const SourceBundle &bundle, const SourceSkin &skin)
    {
        const SourceSkeleton *pSkeleton = CheckSkeletonPath(bundle, skin.Skeleton);
        for (const SourceString &bone : skin.Bones)
            CheckBone(pSkeleton, skin.Skeleton, bone);

        if (skin.BindPoses.size() != skin.Bones.size())
        {
            Error(skin.BindPosesWhere, "expected " + Plural(skin.Bones.size(), "bind pose") + ", one per bone; found "
                + std::to_string(skin.BindPoses.size()));
        }
        for (const SourceTransform &pose : skin.BindPoses)
        {
            if (pose.Rotation)
                CheckRotation(*pose.Rotation);
        }
    }

    // Checks a track's keys. A track of a skeletal animation or a clip.
    void CheckTrack(const SourceTrack &track)
    {
        const size_t components = track.Property == Canvas::CpkgAnimationProperty::Rotation ? 4 : 3;
        const std::vector<float> &times = track.Times.Values;
        const size_t keys = times.size();
        if (keys == 0)
            Error(track.Times.Where, "expected at least one key");
        for (size_t i = 1; i < keys; ++i)
        {
            if (times[i] < times[i - 1])
            {
                Error(track.Times.Where, "expected nondecreasing times; key " + std::to_string(i) + " is earlier than key " + std::to_string(i - 1));
                break;
            }
        }

        const std::vector<float> &values = track.Values.Values;
        if (values.size() != keys * components)
        {
            Error(track.Values.Where, "expected " + Plural(keys * components, "number") + ", " + std::to_string(components) + " for each of "
                + Plural(keys, "key") + "; found " + std::to_string(values.size()));
        }
        else if (components == 4)
        {
            for (size_t i = 0; i < keys; ++i)
            {
                if (values[i * 4] == 0.0f && values[i * 4 + 1] == 0.0f && values[i * 4 + 2] == 0.0f && values[i * 4 + 3] == 0.0f)
                {
                    Error(track.Values.Where, "expected non-zero quaternions; key " + std::to_string(i) + " is zero");
                    break;
                }
            }
        }

        if (track.InterpolationPerKey && track.Interpolations.size() != keys)
        {
            Error(track.InterpolationWhere, "expected " + Plural(keys, "interpolation") + ", one per key; found "
                + std::to_string(track.Interpolations.size()));
        }

        const bool cubic = std::find(track.Interpolations.begin(), track.Interpolations.end(), Canvas::AnimationInterpolation::Cubic)
            != track.Interpolations.end();
        if (cubic && !track.Tangents)
        {
            Error(track.Where, "a track with a \"cubic\" key needs \"tangents\"");
        }
        else if (track.Tangents)
        {
            const size_t found = track.Tangents->Values.size();
            if (!cubic && found != 0)
                Error(track.Tangents->Where, "expected no tangents, since no key is \"cubic\"; found " + std::to_string(found));
            else if (cubic && found != 2 * keys * components)
            {
                Error(track.Tangents->Where, "expected " + Plural(2 * keys * components, "number") + ", two tangents of "
                    + std::to_string(components) + " for each of " + Plural(keys, "key") + "; found " + std::to_string(found));
            }
        }
    }

    void CheckSkeletalAnimation(const SourceBundle &bundle, const SourceSkeletalAnimation &animation)
    {
        const SourceSkeleton *pSkeleton = CheckSkeletonPath(bundle, animation.Skeleton);
        for (size_t i = 0; i < animation.Tracks.size(); ++i)
        {
            const SourceTrack &track = animation.Tracks[i];
            CheckBone(pSkeleton, animation.Skeleton, track.Target);
            CheckTrack(track);
            for (size_t j = 0; j < i; ++j)
            {
                if (animation.Tracks[j].Target.Text == track.Target.Text && animation.Tracks[j].Property == track.Property)
                {
                    Error(track.Target.Where, std::string("the animation already has a \"") + PropertyName(track.Property)
                        + "\" track for " + Quoted(track.Target.Text));
                    break;
                }
            }
        }
    }

    void CheckSkybox(const SourceBundle &bundle, const SourceSkybox &skybox)
    {
        CheckAssetPath(bundle, skybox.Image, AssetType::Image, ImageShape::Cube);
        if (skybox.Orientation)
            CheckRotation(*skybox.Orientation);
    }

    //--------------------------------------------------------------------------------------------
    // Bundle structure

    void CheckLocalAssets(size_t bundleIndex, const CBundleLayout &layout)
    {
        const SourceBundle &bundle = m_Package.Bundles[bundleIndex];
        for (const LocalAsset &asset : layout.GetLocalAssets())
        {
            switch (asset.Type)
            {
            case AssetType::TransformNode:
                CheckNode(bundle, layout, asset);
                break;
            case AssetType::ImportNode:
                CheckImportNode(layout, asset);
                break;
            case AssetType::Placement:
                CheckPlacement(bundleIndex, *std::get<const SourcePlacement *>(asset.pSource));
                break;
            case AssetType::SkeletonInstance:
                CheckSkeletonPath(bundle, std::get<const SourceSkeletonInstance *>(asset.pSource)->Skeleton);
                break;
            case AssetType::MeshInstance:
                CheckMeshInstance(bundle, layout, asset);
                break;
            case AssetType::MeshField:
            {
                const SourceMeshField &field = *std::get<const SourceMeshField *>(asset.pSource);
                CheckAssetPath(bundle, field.Mesh, AssetType::MeshData);
                for (const SourceTransform &transform : field.Transforms)
                    CheckTransform(transform);
                break;
            }
            case AssetType::ExportList:
                CheckExports(bundle, layout, *std::get<const std::vector<SourceString> *>(asset.pSource));
                break;
            default:
                break;
            }
        }
    }

    void CheckNode(const SourceBundle &bundle, const CBundleLayout &layout, const LocalAsset &asset)
    {
        const SourceNode &node = *std::get<const SourceNode *>(asset.pSource);
        CheckTransform(node.Transform);
        if (!node.Bone)
            return;
        if (asset.Reference == -1)
        {
            Error(node.Bone->Where, "a node with \"bone\" needs a parent node with a \"skeletonInstance\"");
            return;
        }
        const SourceSkeletonInstance &instance = *std::get<const SourceSkeletonInstance *>(layout.GetAsset(asset.Reference).pSource);
        CheckBone(FindSkeleton(bundle, instance.Skeleton), instance.Skeleton, *node.Bone);
    }

    void CheckImportNode(const CBundleLayout &layout, const LocalAsset &asset)
    {
        const SourceImportNode &node = *std::get<const SourceImportNode *>(asset.pSource);
        CheckTransform(node.Transform);
        if (asset.Reference == -1)
        {
            if (const LocalAsset *pNamed = layout.FindNamed(node.Placement.Text))
                Error(node.Placement.Where, Quoted(node.Placement.Text) + " is " + AssetTypeNoun(pNamed->Type) + ", not a placement");
            else
                Error(node.Placement.Where, "the bundle has no placement " + Quoted(node.Placement.Text));
            return;
        }
        if (asset.Reference > asset.Index)
        {
            Error(node.Placement.Where, "the placement " + Quoted(node.Placement.Text) + " attaches within this import node");
            return;
        }

        // The export is checked when the placed bundle is in this package.
        const SourcePlacement &placement = *std::get<const SourcePlacement *>(layout.GetAsset(asset.Reference).pSource);
        bool otherPackage = false;
        const SourceBundle *pPlaced = m_Lookup.ResolveBundlePath(placement.Bundle.Text, &otherPackage);
        if (!pPlaced)
            return;
        const auto &exports = pPlaced->Exports;
        if (!exports || std::none_of(exports->begin(), exports->end(), [&](const SourceString &name) { return name.Text == node.Export.Text; }))
            Error(node.Export.Where, "the bundle " + Quoted(pPlaced->Name.Text) + " exports no node " + Quoted(node.Export.Text));
    }

    void CheckPlacement(size_t bundleIndex, const SourcePlacement &placement)
    {
        CheckTransform(placement.Transform);
        bool otherPackage = false;
        const SourceBundle *pPlaced = m_Lookup.ResolveBundlePath(placement.Bundle.Text, &otherPackage);
        if (otherPackage)
            return;
        if (!pPlaced)
        {
            Error(placement.Bundle.Where, "the package has no bundle " + Quoted(PathNames(placement.Bundle.Text).back()));
            return;
        }
        if (pPlaced->World)
        {
            Error(placement.Bundle.Where, Quoted(placement.Bundle.Text) + " is a world bundle, which a bundle path cannot name");
            return;
        }
        const size_t placedIndex = size_t(pPlaced - m_Package.Bundles.data());
        if (placedIndex == bundleIndex)
            Error(placement.Bundle.Where, "the bundle places itself");
        else if (Places(placedIndex, bundleIndex))
            Error(placement.Bundle.Where, Quoted(placement.Bundle.Text) + " places this bundle, directly or through the bundles it places");
    }

    void CheckMeshInstance(const SourceBundle &bundle, const CBundleLayout &layout, const LocalAsset &asset)
    {
        const SourceMeshInstance &instance = *std::get<const SourceMeshInstance *>(asset.pSource);
        CheckAssetPath(bundle, instance.Mesh, AssetType::MeshData);
        if (!instance.Skin)
            return;
        const AssetTarget skin = CheckAssetPath(bundle, *instance.Skin, AssetType::MeshSkin);

        if (asset.SkeletonInstance == -1)
        {
            if (!instance.SkeletonInstance)
                Error(instance.Where, "no node from this mesh instance's node to its root has a \"skeletonInstance\" for the skin to bind to");
            else if (CheckNodeName(bundle, layout, *instance.SkeletonInstance))
                Error(instance.SkeletonInstance->Where, "the node " + Quoted(instance.SkeletonInstance->Text) + " has no \"skeletonInstance\"");
            return;
        }

        // The skin and the skeleton instance it binds are for the same skeleton, when both
        // skeletons are in this package.
        if (skin.State != AssetTarget::Kind::Asset)
            return;
        const SourceSkin &source = skin.pBundle->Skins[skin.Index].Value;
        const SourceSkeletonInstance &bound = *std::get<const SourceSkeletonInstance *>(layout.GetAsset(asset.SkeletonInstance).pSource);
        const AssetTarget skinSkeleton = m_Lookup.ResolveAssetPath(*skin.pBundle, source.Skeleton.Text);
        const AssetTarget boundSkeleton = m_Lookup.ResolveAssetPath(bundle, bound.Skeleton.Text);
        if (skinSkeleton.State == AssetTarget::Kind::Asset && boundSkeleton.State == AssetTarget::Kind::Asset
            && (skinSkeleton.pBundle != boundSkeleton.pBundle || skinSkeleton.Type != boundSkeleton.Type || skinSkeleton.Index != boundSkeleton.Index))
        {
            Error(instance.Skin->Where, "the skin's skeleton " + Quoted(source.Skeleton.Text) + " is not the skeleton instance's "
                + Quoted(bound.Skeleton.Text));
        }
    }

    // Each export names a node. Load reports a repeated export.
    void CheckExports(const SourceBundle &bundle, const CBundleLayout &layout, const std::vector<SourceString> &exports)
    {
        for (const SourceString &name : exports)
            CheckNodeName(bundle, layout, name);
    }

    //--------------------------------------------------------------------------------------------
    // Placement graph

    // The indices of the package's bundles that the bundle places, each once.
    std::vector<size_t> PlacedBundles(const CBundleLayout &layout) const
    {
        std::vector<size_t> placed;
        for (const LocalAsset &asset : layout.GetLocalAssets())
        {
            if (asset.Type != AssetType::Placement)
                continue;
            bool otherPackage = false;
            const SourceBundle *pPlaced = m_Lookup.ResolveBundlePath(std::get<const SourcePlacement *>(asset.pSource)->Bundle.Text, &otherPackage);
            if (!pPlaced)
                continue;
            const size_t index = size_t(pPlaced - m_Package.Bundles.data());
            if (std::find(placed.begin(), placed.end(), index) == placed.end())
                placed.push_back(index);
        }
        return placed;
    }

    // Whether bundle from places bundle to, directly or through the bundles it places.
    bool Places(size_t from, size_t to) const
    {
        std::vector<bool> visited(m_Package.Bundles.size(), false);
        std::vector<size_t> pending = { from };
        visited[from] = true;
        while (!pending.empty())
        {
            const size_t at = pending.back();
            pending.pop_back();
            for (size_t placed : m_Placed[at])
            {
                if (placed == to)
                    return true;
                if (!visited[placed])
                {
                    visited[placed] = true;
                    pending.push_back(placed);
                }
            }
        }
        return false;
    }

    const SourcePackage &m_Package;
    CPackageLookup m_Lookup;
    std::vector<CBundleLayout> m_Layouts;       // per bundle
    std::vector<std::vector<size_t>> m_Placed;  // per bundle, what PlacedBundles returns
    std::vector<Finding> m_Findings;
};

} // namespace

//------------------------------------------------------------------------------------------------
std::vector<std::string_view> PathNames(std::string_view path)
{
    std::vector<std::string_view> names;
    for (size_t start = 0;;)
    {
        const size_t end = path.find('/', start);
        names.push_back(path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos)
            return names;
        start = end + 1;
    }
}

AssetTarget CPackageLookup::ResolveAssetPath(const SourceBundle &from, std::string_view path) const
{
    const std::vector<std::string_view> names = PathNames(path);
    AssetTarget target;
    const SourceBundle *pBundle = &from;
    if (names.size() > 1)
    {
        if (names.size() == 3 && names[0] != m_Package.PackageName.Text)
        {
            target.State = AssetTarget::Kind::OtherPackage;
            return target;
        }
        pBundle = FindBundle(names[names.size() - 2]);
        if (!pBundle)
            return target;
    }

    const std::string_view name = names.back();
    target.pBundle = pBundle;
    target.State = AssetTarget::Kind::Asset;
    if (FindNamed(pBundle->Images, name, &target.Index))
        target.Type = AssetType::Image;
    else if (FindNamed(pBundle->Skyboxes, name, &target.Index))
        target.Type = AssetType::Skybox;
    else if (FindNamed(pBundle->Materials, name, &target.Index))
        target.Type = AssetType::Material;
    else if (FindNamed(pBundle->Meshes, name, &target.Index))
        target.Type = AssetType::MeshData;
    else if (FindNamed(pBundle->Skeletons, name, &target.Index))
        target.Type = AssetType::Skeleton;
    else if (FindNamed(pBundle->Skins, name, &target.Index))
        target.Type = AssetType::MeshSkin;
    else if (FindNamed(pBundle->SkeletalAnimations, name, &target.Index))
        target.Type = AssetType::SkeletalAnimation;
    else
        target.State = pBundle->Imports.empty() ? AssetTarget::Kind::NotFound : AssetTarget::Kind::ByImport;
    return target;
}

const SourceBundle *CPackageLookup::ResolveBundlePath(std::string_view path, bool *pOtherPackage) const
{
    const std::vector<std::string_view> names = PathNames(path);
    *pOtherPackage = names.size() == 2 && names[0] != m_Package.PackageName.Text;
    return *pOtherPackage ? nullptr : FindBundle(names.back());
}

const SourceBundle *CPackageLookup::FindBundle(std::string_view name) const
{
    for (const SourceBundle &bundle : m_Package.Bundles)
    {
        if (bundle.Name.Text == name)
            return &bundle;
    }
    return nullptr;
}

bool ValidatePackageSource(const SourcePackage &package, CDiagnostics &diagnostics)
{
    return CValidator(package).Run(diagnostics);
}

} // namespace CpkgTool
