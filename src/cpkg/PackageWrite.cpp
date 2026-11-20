//================================================================================================
// PackageWrite - the build's Write stage: encodes each asset's payload and writes the package
//================================================================================================

#include "Stages.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace CpkgTool
{

using namespace Canvas;

namespace
{

// q scaled to unit length. q is non-zero; Validate checks it.
Vec4 Normalized(const Vec4 &q)
{
    const double length = std::sqrt(double(q[0]) * q[0] + double(q[1]) * q[1] + double(q[2]) * q[2] + double(q[3]) * q[3]);
    return { float(q[0] / length), float(q[1] / length), float(q[2] / length), float(q[3] / length) };
}

// The transform with its rotation normalized; an omitted part keeps CpkgTransform's identity value.
CpkgTransform ToCpkgTransform(const SourceTransform &source)
{
    CpkgTransform transform;
    if (source.Scale)
        std::memcpy(transform.Scale, source.Scale->data(), sizeof(transform.Scale));
    if (source.Rotation)
    {
        const Vec4 rotation = Normalized(source.Rotation->Quaternion);
        std::memcpy(transform.Rotation, rotation.data(), sizeof(transform.Rotation));
    }
    if (source.Translation)
        std::memcpy(transform.Translation, source.Translation->data(), sizeof(transform.Translation));
    return transform;
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
        m_Diagnostics(diagnostics)
    {
    }

    bool Write(const std::string &path)
    {
        if (!CheckBuildable())
            return false;

        const std::filesystem::path filePath(std::u8string(path.begin(), path.end()));
        std::error_code error;
        std::filesystem::remove(filePath, error);

        Gem::Result result = m_Writer.Open(path.c_str(), m_Package.PackageName.Text);
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
            std::filesystem::remove(filePath, error);
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
            unsupported(!bundle.Nodes.empty(), "nodes");
            unsupported(!bundle.Placements.empty(), "placements");
            unsupported(!bundle.MeshFields.empty(), "meshFields");
            unsupported(!bundle.ImportNodes.empty(), "importNodes");
            unsupported(!bundle.Clips.empty(), "clips");
            unsupported(bundle.Exports.has_value(), "exports");
            unsupported(bundle.World.has_value(), "world");
            unsupported(!bundle.Imports.empty(), "imports");
        }
        return buildable;
    }

    // Finishes encoder's payload with header and appends it to the package as the asset name.
    template <class THeader>
    Gem::Result Append(AssetType type, const SourceString &name, CEncoder<THeader> &encoder, const THeader &header)
    {
        const Gem::Result result = encoder.Finish(header);
        if (Gem::Failed(result))
            return result;
        return m_Writer.AppendAsset(type, name.Text, CpkgCodec::None, encoder.GetPayload());
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
        {
            const Vec4 orientation = Normalized(skybox.Orientation->Quaternion);
            std::memcpy(header.Orientation, orientation.data(), sizeof(header.Orientation));
        }
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

        if (source.Property == CpkgAnimationProperty::Rotation)
        {
            std::vector<float> values;
            for (size_t i = 0; i < keys; ++i)
            {
                const Vec4 q = Normalized({ source.Values.Values[i * 4], source.Values.Values[i * 4 + 1],
                    source.Values.Values[i * 4 + 2], source.Values.Values[i * 4 + 3] });
                values.insert(values.end(), q.begin(), q.end());
            }
            pTrack->Values = encoder.Append(values);
        }
        else
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

    const SourcePackage &m_Package;
    CDiagnostics &m_Diagnostics;
    CCpkgWriter m_Writer;
};

} // namespace

bool WritePackage(const SourcePackage &package, const std::string &path, CDiagnostics &diagnostics)
{
    return CPackageWriter(package, diagnostics).Write(path);
}

} // namespace CpkgTool
