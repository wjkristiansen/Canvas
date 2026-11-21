//================================================================================================
// PackageWrite - the build's Write stage: encodes each asset's payload and writes the package
//================================================================================================

#include "Stages.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace CpkgTool
{

using namespace Canvas;

namespace
{

// The transform; an omitted part keeps CpkgTransform's identity value.
CpkgTransform ToCpkgTransform(const SourceTransform &source)
{
    CpkgTransform transform;
    if (source.Scale)
        std::memcpy(transform.Scale, source.Scale->data(), sizeof(transform.Scale));
    if (source.Rotation)
        std::memcpy(transform.Rotation, source.Rotation->Quaternion.data(), sizeof(transform.Rotation));
    if (source.Translation)
        std::memcpy(transform.Translation, source.Translation->data(), sizeof(transform.Translation));
    return transform;
}

// Whether the files at a and b both exist and hold the same bytes.
bool SameBytes(const std::filesystem::path &a, const std::filesystem::path &b)
{
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(a, error);
    if (error || std::filesystem::file_size(b, error) != size || error)
        return false;

    std::ifstream fileA(a, std::ios::binary);
    std::ifstream fileB(b, std::ios::binary);
    char bufferA[64 * 1024];
    char bufferB[64 * 1024];
    while (fileA && fileB)
    {
        fileA.read(bufferA, sizeof(bufferA));
        fileB.read(bufferB, sizeof(bufferB));
        if (fileA.gcount() != fileB.gcount() || std::memcmp(bufferA, bufferB, size_t(fileA.gcount())) != 0)
            return false;
    }
    return fileA.eof() && fileB.eof();
}

// A temporary file name beside path, unique to this process.
std::string TemporaryPath(const std::string &path)
{
#if defined(_WIN32)
    const int process = _getpid();
#else
    const int process = int(getpid());
#endif
    return path + "." + std::to_string(process) + ".tmp";
}

// Copies values, whose size is a multiple of an element's, into elements of TElement.
template <class TElement, class T>
std::vector<TElement> Elements(const std::vector<T> &values)
{
    static_assert(sizeof(TElement) % sizeof(T) == 0);
    std::vector<TElement> elements(values.size() * sizeof(T) / sizeof(TElement));
    std::memcpy(elements.data(), values.data(), elements.size() * sizeof(TElement));
    return elements;
}

// Builds one payload, keeping the first failure.
template <class THeader>
class CEncoder
{
public:
    // Appends elements to the payload. Returns their span, or an empty span after a failure.
    template <class T>
    CpkgSpan<T> Append(std::span<const T> elements)
    {
        CpkgSpan<T> span;
        if (Gem::Succeeded(m_Result))
            m_Result = m_Builder.Append(elements, &span);
        return span;
    }

    template <class T>
    CpkgSpan<T> Append(const std::vector<T> &elements)
    {
        return Append(std::span<const T>(elements));
    }

    // Appends text's characters, without a terminator.
    CpkgSpan<char> AppendText(std::string_view text)
    {
        return Append(std::span<const char>(text.data(), text.size()));
    }

    // Sets the payload's header. Returns the first failure of this payload, if any.
    Gem::Result Finish(const THeader &header)
    {
        if (Gem::Succeeded(m_Result))
            m_Result = m_Builder.SetHeader(header);
        return m_Result;
    }

    std::span<const std::byte> GetPayload() const { return m_Builder.GetPayload(); }

private:
    CCpkgPayloadBuilder<THeader> m_Builder;
    Gem::Result m_Result = Gem::Result::Success;
};

// Writes one validated package source to a .cpkg file.
class CPackageWriter
{
public:
    CPackageWriter(const SourcePackage &package, CDiagnostics &diagnostics) :
        m_Package(package),
        m_Diagnostics(diagnostics),
        m_Lookup(package)
    {
    }

    // Writes the package to a temporary file beside path, then replaces path with it, unless
    // path already holds the same bytes, which leaves path untouched. A failure leaves path as
    // it was.
    bool Write(const std::string &path)
    {
        if (!CheckBuildable())
            return false;

        const std::string temporary = TemporaryPath(path);
        const std::filesystem::path outputPath(std::u8string(path.begin(), path.end()));
        const std::filesystem::path temporaryPath(std::u8string(temporary.begin(), temporary.end()));
        std::error_code error;
        std::filesystem::remove(temporaryPath, error);

        Gem::Result result = m_Writer.Open(temporary.c_str(), m_Package.PackageName.Text);
        for (const SourceBundle &bundle : m_Package.Bundles)
        {
            if (Gem::Failed(result))
                break;
            result = WriteBundle(bundle);
        }
        if (Gem::Succeeded(result))
            result = m_Writer.Close();

        if (Gem::Failed(result))
        {
            m_Diagnostics.Report(Severity::Error, path, "cannot write the file");
            std::filesystem::remove(temporaryPath, error);
            return false;
        }

        if (SameBytes(temporaryPath, outputPath))
        {
            std::filesystem::remove(temporaryPath, error);
            return true;
        }
        std::filesystem::rename(temporaryPath, outputPath, error);
        if (error)
        {
            m_Diagnostics.Report(Severity::Error, path, "cannot replace the file");
            std::filesystem::remove(temporaryPath, error);
            return false;
        }
        return true;
    }

private:
    // Reports each bundle member this build does not write.
    bool CheckBuildable()
    {
        bool buildable = true;
        for (const SourceBundle &bundle : m_Package.Bundles)
        {
            auto unsupported = [&](bool present, const char *member)
            {
                if (!present)
                    return;
                ReportAt(m_Diagnostics, m_Package, Severity::Error, bundle.Where, std::string("cpkg cannot build a bundle's \"") + member + "\" yet");
                buildable = false;
            };
            unsupported(!bundle.Images.empty(), "images");
            unsupported(!bundle.Imports.empty(), "imports");
        }
        return buildable;
    }

    // Finishes encoder's payload with header and appends it to the package as the asset name.
    template <class THeader>
    Gem::Result Append(AssetType type, std::string_view name, CEncoder<THeader> &encoder, const THeader &header)
    {
        const Gem::Result result = encoder.Finish(header);
        if (Gem::Failed(result))
            return result;
        return m_Writer.AppendAsset(type, name, CpkgCodec::None, encoder.GetPayload());
    }

    template <class THeader>
    Gem::Result Append(AssetType type, const SourceString &name, CEncoder<THeader> &encoder, const THeader &header)
    {
        return Append(type, std::string_view(name.Text), encoder, header);
    }

    // Appends the bundle's assets in CanvasPkg's canonical order.
    Gem::Result WriteBundle(const SourceBundle &bundle)
    {
        Gem::Result result = m_Writer.BeginBundle(bundle.Name.Text);
        auto each = [&](const auto &assets, auto write)
        {
            for (const auto &asset : assets)
            {
                if (Gem::Failed(result))
                    return;
                result = (this->*write)(bundle, asset);
            }
        };
        each(bundle.Skyboxes, &CPackageWriter::WriteSkybox);
        each(bundle.Materials, &CPackageWriter::WriteMaterial);
        each(bundle.Meshes, &CPackageWriter::WriteMesh);
        each(bundle.Skeletons, &CPackageWriter::WriteSkeleton);
        each(bundle.Skins, &CPackageWriter::WriteSkin);
        each(bundle.SkeletalAnimations, &CPackageWriter::WriteSkeletalAnimation);

        const CBundleLayout layout(bundle);
        for (const LocalAsset &asset : layout.GetLocalAssets())
        {
            if (Gem::Failed(result))
                break;
            result = WriteLocalAsset(layout, asset);
        }

        if (Gem::Succeeded(result))
            result = m_Writer.EndBundle();
        return result;
    }

    //--------------------------------------------------------------------------------------------
    // Payloads

    Gem::Result WriteSkybox(const SourceBundle &, const SourceNamed<SourceSkybox> &asset)
    {
        const SourceSkybox &skybox = asset.Value;
        CEncoder<CpkgSkyboxData> encoder;
        CpkgSkyboxData header;
        header.Image = encoder.AppendText(skybox.Image.Text);
        if (skybox.Orientation)
            std::memcpy(header.Orientation, skybox.Orientation->Quaternion.data(), sizeof(header.Orientation));
        header.Intensity = skybox.Intensity;
        return Append(AssetType::Skybox, asset.Name, encoder, header);
    }

    Gem::Result WriteMaterial(const SourceBundle &, const SourceNamed<SourceMaterial> &asset)
    {
        const SourceMaterial &material = asset.Value;
        CEncoder<CpkgMaterialData> encoder;
        CpkgMaterialData header;
        std::memcpy(header.BaseColorFactor, material.BaseColor.data(), sizeof(header.BaseColorFactor));
        std::memcpy(header.EmissiveFactor, material.Emissive.data(), 3 * sizeof(float));
        header.RoughMetalAOFactor[0] = material.Roughness;
        header.RoughMetalAOFactor[1] = material.Metallic;
        header.RoughMetalAOFactor[2] = material.AmbientOcclusion;

        CpkgSpan<char> *textures[] = {
            &header.AlbedoTexture, &header.NormalTexture, &header.RoughnessTexture,
            &header.MetallicTexture, &header.AmbientOcclusionTexture, &header.EmissiveTexture,
        };
        static_assert(std::size(textures) == size_t(SourceTextureRole::Count));
        for (size_t role = 0; role < std::size(textures); ++role)
        {
            if (material.Textures[role])
                *textures[role] = encoder.AppendText(material.Textures[role]->Text);
        }

        if (material.Displacement)
        {
            const SourceDisplacement &displacement = *material.Displacement;
            header.HasDisplacement = 1;
            header.Displacement.DisplacementMap = encoder.AppendText(displacement.Map.Text);
            header.Displacement.MapScale = displacement.Scale;
            header.Displacement.MapBias = displacement.Bias;
            header.Displacement.MinTessFactor = displacement.MinTessFactor;
            header.Displacement.MaxTessFactor = displacement.MaxTessFactor;
            header.Displacement.DistanceLodScale = displacement.DistanceLodScale;
            header.Displacement.CurvatureLodScale = displacement.CurvatureLodScale;
        }
        return Append(AssetType::Material, asset.Name, encoder, header);
    }

    Gem::Result WriteMesh(const SourceBundle &, const SourceNamed<SourceMesh> &asset)
    {
        const SourceMesh &mesh = asset.Value;
        CEncoder<CpkgMeshData> encoder;
        CpkgMeshData header;
        header.Topology = mesh.Topology;

        std::vector<CpkgMeshDataGroup> groups;
        for (const SourceMeshGroup &source : mesh.Groups)
        {
            CpkgMeshDataGroup &group = groups.emplace_back();
            if (source.Material)
                group.Material = encoder.AppendText(source.Material->Text);
            group.Positions = encoder.Append(Elements<CpkgVertexPosition>(source.Positions.Values));
            if (source.Normals)
                group.Normals = encoder.Append(Elements<CpkgVertexNormal>(source.Normals->Values));
            if (source.UV0)
                group.UV0 = encoder.Append(Elements<CpkgVertexUV>(source.UV0->Values));
            if (source.Tangents)
                group.Tangents = encoder.Append(Elements<CpkgVertexTangent>(source.Tangents->Values));
            if (source.BoneIndices)
                group.BoneIndices = encoder.Append(Elements<CpkgVertexBoneIndices>(source.BoneIndices->Values));
            if (source.BoneWeights)
                group.BoneWeights = encoder.Append(Elements<CpkgVertexBoneWeights>(source.BoneWeights->Values));

            if (mesh.BoundsForm == SourceBoundsForm::Computed)
            {
                const std::vector<float> &positions = source.Positions.Values;
                for (size_t i = 0; i + 2 < positions.size(); i += 3)
                {
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        header.LocalBounds.Min[axis] = std::min(header.LocalBounds.Min[axis], positions[i + axis]);
                        header.LocalBounds.Max[axis] = std::max(header.LocalBounds.Max[axis], positions[i + axis]);
                    }
                }
            }
        }
        header.Groups = encoder.Append(groups);

        if (mesh.BoundsForm == SourceBoundsForm::Declared)
        {
            std::memcpy(header.LocalBounds.Min, mesh.Bounds.Min.data(), sizeof(header.LocalBounds.Min));
            std::memcpy(header.LocalBounds.Max, mesh.Bounds.Max.data(), sizeof(header.LocalBounds.Max));
        }
        return Append(AssetType::MeshData, asset.Name, encoder, header);
    }

    // The bone index of the earlier bone named name, which Validate found.
    static int32_t BoneIndex(const SourceSkeleton &skeleton, std::string_view name)
    {
        for (size_t i = 0; i < skeleton.Bones.size(); ++i)
        {
            if (skeleton.Bones[i].Name.Text == name)
                return int32_t(i);
        }
        return -1;
    }

    Gem::Result WriteSkeleton(const SourceBundle &, const SourceNamed<SourceSkeleton> &asset)
    {
        const SourceSkeleton &skeleton = asset.Value;
        CEncoder<CpkgSkeletonData> encoder;
        std::vector<CpkgSkeletonBone> bones;
        for (const SourceBone &source : skeleton.Bones)
        {
            CpkgSkeletonBone &bone = bones.emplace_back();
            bone.ParentIndex = source.Parent ? BoneIndex(skeleton, source.Parent->Text) : -1;
            bone.Name = encoder.AppendText(source.Name.Text);
            bone.RestTransform = ToCpkgTransform(source.Rest);
        }
        CpkgSkeletonData header;
        header.Bones = encoder.Append(bones);
        return Append(AssetType::Skeleton, asset.Name, encoder, header);
    }

    Gem::Result WriteSkin(const SourceBundle &, const SourceNamed<SourceSkin> &asset)
    {
        const SourceSkin &skin = asset.Value;
        CEncoder<CpkgMeshSkinData> encoder;
        CpkgMeshSkinData header;
        header.Skeleton = encoder.AppendText(skin.Skeleton.Text);

        std::vector<CpkgSpan<char>> bones;
        for (const SourceString &bone : skin.Bones)
            bones.push_back(encoder.AppendText(bone.Text));
        header.Bones = encoder.Append(bones);

        std::vector<CpkgTransform> poses;
        for (const SourceTransform &pose : skin.BindPoses)
            poses.push_back(ToCpkgTransform(pose));
        header.BindPoses = encoder.Append(poses);
        return Append(AssetType::MeshSkin, asset.Name, encoder, header);
    }

    // Encodes a track's keys into the fields every track has.
    template <class THeader, class TTrack>
    static void EncodeKeys(CEncoder<THeader> &encoder, const SourceTrack &source, TTrack *pTrack)
    {
        const size_t keys = source.Times.Values.size();
        pTrack->Property = source.Property;
        pTrack->Times = encoder.Append(source.Times.Values);
        pTrack->Values = encoder.Append(source.Values.Values);

        // Interpolations are stored per key unless every key's is Linear; tangents only for a
        // track with a Cubic key.
        const auto &interpolations = source.Interpolations;
        const bool linear = std::all_of(interpolations.begin(), interpolations.end(),
            [](AnimationInterpolation i) { return i == AnimationInterpolation::Linear; });
        const bool cubic = std::find(interpolations.begin(), interpolations.end(), AnimationInterpolation::Cubic) != interpolations.end();
        if (!linear)
        {
            pTrack->Interpolations = encoder.Append(source.InterpolationPerKey
                ? interpolations : std::vector<AnimationInterpolation>(keys, interpolations[0]));
        }
        if (cubic)
            pTrack->Tangents = encoder.Append(source.Tangents->Values);
    }

    Gem::Result WriteSkeletalAnimation(const SourceBundle &, const SourceNamed<SourceSkeletalAnimation> &asset)
    {
        const SourceSkeletalAnimation &animation = asset.Value;
        CEncoder<CpkgSkeletalAnimationData> encoder;
        CpkgSkeletalAnimationData header;
        header.Skeleton = encoder.AppendText(animation.Skeleton.Text);

        std::vector<CpkgBoneTrack> tracks;
        for (const SourceTrack &source : animation.Tracks)
        {
            CpkgBoneTrack &track = tracks.emplace_back();
            track.Bone = encoder.AppendText(source.Target.Text);
            EncodeKeys(encoder, source, &track);
        }
        header.Tracks = encoder.Append(tracks);
        return Append(AssetType::SkeletalAnimation, asset.Name, encoder, header);
    }

    //--------------------------------------------------------------------------------------------
    // Bundle structure

    template <class TSource>
    static const TSource &SourceOf(const LocalAsset &asset)
    {
        return *std::get<const TSource *>(asset.pSource);
    }

    Gem::Result WriteLocalAsset(const CBundleLayout &layout, const LocalAsset &asset)
    {
        const std::string_view name = asset.pName ? std::string_view(asset.pName->Text) : std::string_view();
        switch (asset.Type)
        {
        case AssetType::TransformNode:
        {
            const SourceNode &node = SourceOf<SourceNode>(asset);
            CEncoder<CpkgTransformNodeData> encoder;
            CpkgTransformNodeData header;
            header.ParentIndex = asset.Reference;
            header.Mobility = node.Mobility.value_or(CpkgMobility::Static);
            if (node.Bone)
                header.ParentBone = encoder.AppendText(node.Bone->Text);
            header.LocalTransform = ToCpkgTransform(node.Transform);
            return Append(asset.Type, name, encoder, header);
        }

        case AssetType::ImportNode:
        {
            const SourceImportNode &node = SourceOf<SourceImportNode>(asset);
            CEncoder<CpkgImportNodeData> encoder;
            CpkgImportNodeData header;
            header.PlacementIndex = asset.Reference;
            header.ExportName = encoder.AppendText(node.Export.Text);
            header.LocalTransform = ToCpkgTransform(node.Transform);
            return Append(asset.Type, name, encoder, header);
        }

        case AssetType::Placement:
        {
            const SourcePlacement &placement = SourceOf<SourcePlacement>(asset);
            CEncoder<CpkgPlacementData> encoder;
            CpkgPlacementData header;
            header.NodeIndex = asset.Reference;
            header.Mobility = placement.Mobility.value_or(CpkgMobility::Static);
            header.Bundle = encoder.AppendText(placement.Bundle.Text);
            header.Transform = ToCpkgTransform(placement.Transform);
            return Append(asset.Type, name, encoder, header);
        }

        case AssetType::SkeletonInstance:
        {
            CEncoder<CpkgSkeletonInstanceData> encoder;
            CpkgSkeletonInstanceData header;
            header.NodeIndex = asset.Reference;
            header.Skeleton = encoder.AppendText(SourceOf<SourceSkeletonInstance>(asset).Skeleton.Text);
            return Append(asset.Type, name, encoder, header);
        }

        case AssetType::MeshInstance:
        {
            const SourceMeshInstance &instance = SourceOf<SourceMeshInstance>(asset);
            CEncoder<CpkgMeshInstanceData> encoder;
            CpkgMeshInstanceData header;
            header.NodeIndex = asset.Reference;
            header.SkeletonInstanceIndex = asset.SkeletonInstance;
            header.MeshData = encoder.AppendText(instance.Mesh.Text);
            if (instance.Skin)
                header.Skin = encoder.AppendText(instance.Skin->Text);
            return Append(asset.Type, name, encoder, header);
        }

        case AssetType::MeshField:
        {
            const SourceMeshField &field = SourceOf<SourceMeshField>(asset);
            CEncoder<CpkgMeshFieldData> encoder;
            CpkgMeshFieldData header;
            header.NodeIndex = asset.Reference;
            header.CullDistance = field.CullDistance;
            header.MeshData = encoder.AppendText(field.Mesh.Text);
            std::vector<CpkgTransform> transforms;
            for (const SourceTransform &transform : field.Transforms)
                transforms.push_back(ToCpkgTransform(transform));
            header.Transforms = encoder.Append(transforms);
            return Append(asset.Type, name, encoder, header);
        }

        case AssetType::Camera:
        {
            const SourceCamera &camera = SourceOf<SourceCamera>(asset);
            CEncoder<CpkgCameraData> encoder;
            CpkgCameraData header;
            header.NodeIndex = asset.Reference;
            header.NearClip = camera.NearClip.value_or(header.NearClip);
            header.FarClip = camera.FarClip.value_or(header.FarClip);
            header.FovAngle = camera.FovAngle.value_or(header.FovAngle);
            header.ExposureStops = camera.ExposureStops.value_or(header.ExposureStops);
            return Append(asset.Type, name, encoder, header);
        }

        case AssetType::Light:
            return WriteLight(asset, name);

        case AssetType::AnimationClip:
            return WriteClip(layout, asset, name);

        case AssetType::WorldSettings:
            return WriteWorld(layout, asset, name);

        case AssetType::ExportList:
        {
            CEncoder<CpkgExportListData> encoder;
            std::vector<int32_t> nodes;
            for (const SourceString &exported : SourceOf<std::vector<SourceString>>(asset))
                nodes.push_back(layout.FindNamed(exported.Text)->Index);
            CpkgExportListData header;
            header.NodeIndices = encoder.Append(nodes);
            return Append(asset.Type, name, encoder, header);
        }

        default:
            return Gem::Result::Fail;
        }
    }

    Gem::Result WriteLight(const LocalAsset &asset, std::string_view name)
    {
        const SourceLight &light = SourceOf<SourceLight>(asset);
        CEncoder<CpkgLightData> encoder;
        CpkgLightData header;
        header.NodeIndex = asset.Reference;
        header.Type = light.Type.value_or(header.Type);
        if (light.Color)
            std::memcpy(header.Color, light.Color->data(), 3 * sizeof(float));
        header.Intensity = light.Intensity.value_or(header.Intensity);
        header.Flags = LightFlags::None;
        if (light.Enabled.value_or(true))
            header.Flags |= LightFlags::Enabled;
        if (light.CastsShadows.value_or(false))
            header.Flags |= LightFlags::CastsShadows;
        header.AttenuationConstant = light.AttenuationConstant.value_or(header.AttenuationConstant);
        header.AttenuationLinear = light.AttenuationLinear.value_or(header.AttenuationLinear);
        header.AttenuationQuadratic = light.AttenuationQuadratic.value_or(header.AttenuationQuadratic);
        header.Range = light.Range.value_or(header.Range);
        header.SpotInnerAngle = light.SpotInnerAngle.value_or(header.SpotInnerAngle);
        header.SpotOuterAngle = light.SpotOuterAngle.value_or(header.SpotOuterAngle);
        header.ShadowResolution = light.ShadowResolution.value_or(header.ShadowResolution);
        header.ShadowConstantBias = light.ShadowConstantBias.value_or(header.ShadowConstantBias);
        header.ShadowSlopeScaleBias = light.ShadowSlopeScaleBias.value_or(header.ShadowSlopeScaleBias);
        header.ShadowNormalOffset = light.ShadowNormalOffset.value_or(header.ShadowNormalOffset);
        return Append(asset.Type, name, encoder, header);
    }

    Gem::Result WriteClip(const CBundleLayout &layout, const LocalAsset &asset, std::string_view name)
    {
        const SourceClip &clip = SourceOf<SourceClip>(asset);
        CEncoder<CpkgAnimationClipData> encoder;
        CpkgAnimationClipData header;

        // The default duration is the latest key time: a track's last, since its times are
        // nondecreasing. Validate requires a duration of a clip that binds an animation in
        // another package.
        float duration = 0.0f;
        std::vector<CpkgNodeTrack> tracks;
        for (const SourceTrack &source : clip.Tracks)
        {
            CpkgNodeTrack &track = tracks.emplace_back();
            track.NodeIndex = layout.FindNamed(source.Target.Text)->Index;
            EncodeKeys(encoder, source, &track);
            duration = std::max(duration, source.Times.Values.back());
        }
        header.Tracks = encoder.Append(tracks);

        std::vector<CpkgSkeletalAnimationBinding> bindings;
        for (const SourceClipBinding &source : clip.SkeletalAnimations)
        {
            CpkgSkeletalAnimationBinding &binding = bindings.emplace_back();
            binding.SkeletonInstanceIndex = layout.FindNamed(source.SkeletonInstance.Text)->SkeletonInstance;
            binding.Animation = encoder.AppendText(source.Animation.Text);
            const AssetTarget target = m_Lookup.ResolveAssetPath(layout.GetBundle(), source.Animation.Text);
            if (target.State == AssetTarget::Kind::Asset)
            {
                for (const SourceTrack &track : target.pBundle->SkeletalAnimations[target.Index].Value.Tracks)
                    duration = std::max(duration, track.Times.Values.back());
            }
        }
        header.SkeletalAnimations = encoder.Append(bindings);

        header.Duration = clip.Duration.value_or(duration);
        return Append(asset.Type, name, encoder, header);
    }

    Gem::Result WriteWorld(const CBundleLayout &layout, const LocalAsset &asset, std::string_view name)
    {
        const SourceWorld &world = SourceOf<SourceWorld>(asset);
        CEncoder<CpkgWorldSettingsData> encoder;
        CpkgWorldSettingsData header;

        std::vector<CpkgWorldRegionRef> regions;
        for (const SourceRegion &source : world.Regions)
        {
            CpkgWorldRegionRef &region = regions.emplace_back();
            region.Region = encoder.AppendText(source.Region.Text);
            std::memcpy(region.StreamingBounds.Min, source.Bounds.Min.data(), sizeof(region.StreamingBounds.Min));
            std::memcpy(region.StreamingBounds.Max, source.Bounds.Max.data(), sizeof(region.StreamingBounds.Max));
        }
        header.Regions = encoder.Append(regions);

        std::vector<CpkgSpan<char>> skyboxes;
        for (size_t i = 0; i < world.Skyboxes.size(); ++i)
        {
            skyboxes.push_back(encoder.AppendText(world.Skyboxes[i].Text));
            if (world.DefaultSkybox && header.DefaultSkybox == -1 && world.Skyboxes[i].Text == world.DefaultSkybox->Text)
                header.DefaultSkybox = int32_t(i);
        }
        header.Skyboxes = encoder.Append(skyboxes);

        if (world.ActiveCamera)
            header.ActiveCameraIndex = layout.CameraOf(layout.FindNamed(world.ActiveCamera->Text)->Index);
        std::memcpy(header.BackgroundColor, world.BackgroundColor.data(), sizeof(header.BackgroundColor));
        return Append(asset.Type, name, encoder, header);
    }

    const SourcePackage &m_Package;
    CDiagnostics &m_Diagnostics;
    CPackageLookup m_Lookup;
    CCpkgWriter m_Writer;
};

} // namespace

bool WritePackage(const SourcePackage &package, const std::string &path, CDiagnostics &diagnostics)
{
    return CPackageWriter(package, diagnostics).Write(path);
}

} // namespace CpkgTool
