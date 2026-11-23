//================================================================================================
// Dump - prints the contents of a .cpkg file
//================================================================================================

#include "CpkgTool.h"
#include "Stages.h"

namespace CpkgTool
{

using namespace Canvas;

namespace
{
    // Reads the format version from the file header of a package the reader rejects for it.
    bool ReadVersion(const std::string &package, uint32_t *pVersion)
    {
        std::string bytes;
        if (ReadFileRange(CpkgTool::PathOf(package), 0, sizeof(CpkgFileHeader), &bytes) != FileResult::Success)
            return false;
        CpkgFileHeader header;
        std::memcpy(&header, bytes.data(), sizeof(header));
        *pVersion = header.Version;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    // Names

    const char *MobilityName(CpkgMobility value)
    {
        switch (value)
        {
        case CpkgMobility::Static:      return "Static";
        case CpkgMobility::Stationary:  return "Stationary";
        case CpkgMobility::Movable:     return "Movable";
        default:                        return nullptr;
        }
    }

    const char *TopologyName(GfxPrimitiveTopology value)
    {
        switch (value)
        {
        case GfxPrimitiveTopology::TriangleList:  return "TriangleList";
        case GfxPrimitiveTopology::PatchList4CP:  return "PatchList4CP";
        default:                                  return nullptr;
        }
    }

    const char *EncodingName(CpkgImageEncoding value)
    {
        switch (value)
        {
        case CpkgImageEncoding::Unknown:  return "Unknown";
        case CpkgImageEncoding::Jpeg:     return "Jpeg";
        case CpkgImageEncoding::Png:      return "Png";
        case CpkgImageEncoding::Tiff:     return "Tiff";
        default:                          return nullptr;
        }
    }

    const char *DimensionName(CpkgImageDimension value)
    {
        switch (value)
        {
        case CpkgImageDimension::Texture1D:  return "Texture1D";
        case CpkgImageDimension::Texture2D:  return "Texture2D";
        case CpkgImageDimension::Texture3D:  return "Texture3D";
        case CpkgImageDimension::Cube:       return "Cube";
        default:                             return nullptr;
        }
    }

    const char *PropertyName(CpkgAnimationProperty value)
    {
        switch (value)
        {
        case CpkgAnimationProperty::Translation:  return "Translation";
        case CpkgAnimationProperty::Rotation:     return "Rotation";
        case CpkgAnimationProperty::Scale:        return "Scale";
        default:                                  return nullptr;
        }
    }

    const char *LightTypeName(LightType value)
    {
        switch (value)
        {
        case LightType::Ambient:      return "Ambient";
        case LightType::Point:        return "Point";
        case LightType::Directional:  return "Directional";
        case LightType::Spot:         return "Spot";
        case LightType::Area:         return "Area";
        default:                      return nullptr;
        }
    }

    // The enumerator's name, or its value for a value with no name.
    template <class TEnum>
    std::string EnumText(TEnum value, const char *(*pName)(TEnum))
    {
        const char *pText = pName(value);
        return pText ? std::string(pText) : std::to_string(static_cast<std::underlying_type_t<TEnum>>(value));
    }

    std::string LightFlagsText(LightFlags flags)
    {
        uint32_t bits = static_cast<uint32_t>(flags);
        if (bits == 0)
            return "None";

        std::string text;
        auto append = [&](uint32_t bit, const char *pName)
        {
            if (bits & bit)
            {
                if (!text.empty())
                    text += " | ";
                text += pName;
                bits &= ~bit;
            }
        };
        append(static_cast<uint32_t>(LightFlags::CastsShadows), "CastsShadows");
        append(static_cast<uint32_t>(LightFlags::Enabled), "Enabled");
        if (bits != 0)
        {
            if (!text.empty())
                text += " | ";
            text += std::to_string(bits);
        }
        return text;
    }

    //--------------------------------------------------------------------------------------------
    // Values

    std::string FloatText(float value)
    {
        char buffer[32];
        const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, end);
    }

    std::string FloatsText(const float *pValues, size_t count)
    {
        std::string text;
        for (size_t i = 0; i < count; ++i)
        {
            if (i > 0)
                text += ", ";
            text += FloatText(pValues[i]);
        }
        return text;
    }

    template <size_t N>
    std::string FloatsText(const float (&values)[N])
    {
        return FloatsText(values, N);
    }

    std::string Plural(uint64_t count, const char *pNoun)
    {
        return std::to_string(count) + " " + pNoun + (count == 1 ? "" : "s");
    }

    //--------------------------------------------------------------------------------------------
    // Payloads

    struct PayloadError
    {
        std::string Message;
    };

    // Writes a payload's fields as "Name: value" lines, one level of indentation per nesting
    // level, checking each span it reads against the Payload Layout rules.
    template <class THeader>
    class CPayloadPrinter
    {
    public:
        CPayloadPrinter(std::span<const std::byte> payload, std::ostream &out, int indent) :
            m_Payload(payload),
            m_Out(out),
            m_Indent(indent)
        {
        }

        const THeader &Header(const char *pTypeName)
        {
            const THeader *pHeader = nullptr;
            if (Gem::Failed(CpkgGetPayloadHeader(m_Payload, &pHeader)))
                throw PayloadError{ std::string("the payload is too small for its ") + pTypeName + " header" };
            return *pHeader;
        }

        void Field(std::string_view name, std::string_view value)
        {
            WriteIndent();
            m_Out << name << ": " << value << '\n';
        }

        void Field(std::string_view name, int64_t value) { Field(name, std::to_string(value)); }
        void Field(std::string_view name, float value) { Field(name, FloatText(value)); }

        template <size_t N>
        void Field(std::string_view name, const float (&values)[N])
        {
            Field(name, FloatsText(values));
        }

        // Begins a nested structure; End ends it.
        void Begin(std::string_view name)
        {
            WriteIndent();
            m_Out << name << ":\n";
            Push(name);
        }

        void End() { Pop(); }

        void String(std::string_view name, const CpkgSpan<char> &span)
        {
            Field(name, Quoted(StringOf(span, name)));
        }

        // An array of numeric data: its element count.
        template <class T>
        void Count(std::string_view name, const CpkgSpan<T> &span)
        {
            Field(name, int64_t(Resolve(span, name).size()));
        }

        // An array of structures: its element count, then each element as writeElement writes it.
        template <class T, class TWrite>
        void Elements(std::string_view name, const CpkgSpan<T> &span, TWrite &&writeElement)
        {
            const std::span<const T> elements = Resolve(span, name);
            Field(name, int64_t(elements.size()));
            Push(name);
            for (size_t i = 0; i < elements.size(); ++i)
            {
                const std::string element = "[" + std::to_string(i) + "]";
                Begin(element);
                writeElement(elements[i]);
                End();
            }
            Pop();
        }

        // An array of arrays: its element count, then each element as text(elements) writes it.
        template <class T, class TText>
        void List(std::string_view name, const CpkgSpan<CpkgSpan<T>> &span, TText &&text)
        {
            const std::span<const CpkgSpan<T>> spans = Resolve(span, name);
            Field(name, int64_t(spans.size()));
            Push(name);
            for (size_t i = 0; i < spans.size(); ++i)
            {
                const std::string element = "[" + std::to_string(i) + "]";
                Field(element, text(Resolve(spans[i], element)));
            }
            Pop();
        }

        void Strings(std::string_view name, const CpkgSpan<CpkgSpan<char>> &span)
        {
            List(name, span, [](std::span<const char> chars) { return Quoted(std::string_view(chars.data(), chars.size())); });
        }

        void Transform(std::string_view name, const CpkgTransform &transform)
        {
            Begin(name);
            Field("Scale", transform.Scale);
            Field("Rotation", transform.Rotation);
            Field("Translation", transform.Translation);
            End();
        }

        void Bounds(std::string_view name, const CpkgAABB &bounds)
        {
            Begin(name);
            Field("Min", bounds.Min);
            Field("Max", bounds.Max);
            End();
        }

        template <class T>
        std::span<const T> Resolve(const CpkgSpan<T> &span, std::string_view name)
        {
            std::span<const T> elements;
            if (Gem::Failed(CpkgResolveSpan<THeader>(m_Payload, span, &elements)))
                throw PayloadError{ PathOf(name) + " breaks the payload layout rules" };
            return elements;
        }

    private:
        std::string_view StringOf(const CpkgSpan<char> &span, std::string_view name)
        {
            const std::span<const char> chars = Resolve(span, name);
            return std::string_view(chars.data(), chars.size());
        }

        void WriteIndent()
        {
            m_Out << std::string(size_t(m_Indent + 2 * int(m_Path.size())), ' ');
        }

        void Push(std::string_view name) { m_Path.emplace_back(name); }
        void Pop() { m_Path.pop_back(); }

        std::string PathOf(std::string_view name) const
        {
            std::string path;
            auto append = [&path](std::string_view segment)
            {
                if (!path.empty() && segment.front() != '[')
                    path += '.';
                path += segment;
            };
            for (const std::string &segment : m_Path)
                append(segment);
            append(name);
            return path;
        }

        std::span<const std::byte> m_Payload;
        std::ostream &m_Out;
        int m_Indent;
        std::vector<std::string> m_Path;
    };

    void PrintPayload(AssetType type, std::span<const std::byte> payload, std::ostream &out)
    {
        switch (type)
        {
        case AssetType::TransformNode:
        {
            CPayloadPrinter<CpkgTransformNodeData> p(payload, out, 4);
            const auto &h = p.Header("CpkgTransformNodeData");
            p.Field("ParentIndex", int64_t(h.ParentIndex));
            p.Field("Mobility", EnumText(h.Mobility, MobilityName));
            p.String("ParentBone", h.ParentBone);
            p.Transform("LocalTransform", h.LocalTransform);
            break;
        }
        case AssetType::MeshData:
        {
            CPayloadPrinter<CpkgMeshData> p(payload, out, 4);
            const auto &h = p.Header("CpkgMeshData");
            p.Field("Topology", EnumText(h.Topology, TopologyName));
            p.Bounds("LocalBounds", h.LocalBounds);
            p.Elements("Groups", h.Groups, [&p](const CpkgMeshDataGroup &group)
            {
                p.String("Material", group.Material);
                p.Count("Positions", group.Positions);
                p.Count("Normals", group.Normals);
                p.Count("UV0", group.UV0);
                p.Count("Tangents", group.Tangents);
                p.Count("BoneIndices", group.BoneIndices);
                p.Count("BoneWeights", group.BoneWeights);
            });
            break;
        }
        case AssetType::Material:
        {
            CPayloadPrinter<CpkgMaterialData> p(payload, out, 4);
            const auto &h = p.Header("CpkgMaterialData");
            p.Field("BaseColorFactor", h.BaseColorFactor);
            p.Field("EmissiveFactor", h.EmissiveFactor);
            p.Field("RoughMetalAOFactor", h.RoughMetalAOFactor);
            p.String("AlbedoTexture", h.AlbedoTexture);
            p.String("NormalTexture", h.NormalTexture);
            p.String("RoughnessTexture", h.RoughnessTexture);
            p.String("MetallicTexture", h.MetallicTexture);
            p.String("AmbientOcclusionTexture", h.AmbientOcclusionTexture);
            p.String("EmissiveTexture", h.EmissiveTexture);
            p.Field("HasDisplacement", int64_t(h.HasDisplacement));
            if (h.HasDisplacement == 1)
            {
                const CpkgDisplacementData &d = h.Displacement;
                p.Begin("Displacement");
                p.String("DisplacementMap", d.DisplacementMap);
                p.Field("MapScale", d.MapScale);
                p.Field("MapBias", d.MapBias);
                p.Field("MinTessFactor", d.MinTessFactor);
                p.Field("MaxTessFactor", d.MaxTessFactor);
                p.Field("DistanceLodScale", d.DistanceLodScale);
                p.Field("CurvatureLodScale", d.CurvatureLodScale);
                p.End();
            }
            break;
        }
        case AssetType::Image:
        {
            CPayloadPrinter<CpkgImageData> p(payload, out, 4);
            const auto &h = p.Header("CpkgImageData");
            p.Field("Encoding", EnumText(h.Encoding, EncodingName));
            p.Field("Dimension", EnumText(h.Dimension, DimensionName));
            p.Field("Width", int64_t(h.Width));
            p.Field("Height", int64_t(h.Height));
            p.Field("Depth", int64_t(h.Depth));
            p.Field("ArraySize", int64_t(h.ArraySize));
            p.Field("MipLevels", int64_t(h.MipLevels));
            p.List("Images", h.Images, [](std::span<const uint8_t> bytes) { return Plural(bytes.size(), "byte"); });
            break;
        }
        case AssetType::AnimationClip:
        {
            CPayloadPrinter<CpkgAnimationClipData> p(payload, out, 4);
            const auto &h = p.Header("CpkgAnimationClipData");
            p.Field("Duration", h.Duration);
            p.Elements("Tracks", h.Tracks, [&p](const CpkgNodeTrack &track)
            {
                p.Field("NodeIndex", int64_t(track.NodeIndex));
                p.Field("Property", EnumText(track.Property, PropertyName));
                p.Count("Times", track.Times);
                p.Count("Values", track.Values);
                p.Count("Interpolations", track.Interpolations);
                p.Count("Tangents", track.Tangents);
            });
            p.Elements("SkeletalAnimations", h.SkeletalAnimations, [&p](const CpkgSkeletalAnimationBinding &binding)
            {
                p.Field("SkeletonInstanceIndex", int64_t(binding.SkeletonInstanceIndex));
                p.String("Animation", binding.Animation);
            });
            break;
        }
        case AssetType::MeshSkin:
        {
            CPayloadPrinter<CpkgMeshSkinData> p(payload, out, 4);
            const auto &h = p.Header("CpkgMeshSkinData");
            p.String("Skeleton", h.Skeleton);
            p.Strings("Bones", h.Bones);
            p.Elements("BindPoses", h.BindPoses, [&p](const CpkgTransform &pose)
            {
                p.Field("Scale", pose.Scale);
                p.Field("Rotation", pose.Rotation);
                p.Field("Translation", pose.Translation);
            });
            break;
        }
        case AssetType::Camera:
        {
            CPayloadPrinter<CpkgCameraData> p(payload, out, 4);
            const auto &h = p.Header("CpkgCameraData");
            p.Field("NodeIndex", int64_t(h.NodeIndex));
            p.Field("NearClip", h.NearClip);
            p.Field("FarClip", h.FarClip);
            p.Field("FovAngle", h.FovAngle);
            p.Field("ExposureStops", h.ExposureStops);
            break;
        }
        case AssetType::Light:
        {
            CPayloadPrinter<CpkgLightData> p(payload, out, 4);
            const auto &h = p.Header("CpkgLightData");
            p.Field("NodeIndex", int64_t(h.NodeIndex));
            p.Field("Type", EnumText(h.Type, LightTypeName));
            p.Field("Color", h.Color);
            p.Field("Intensity", h.Intensity);
            p.Field("Flags", LightFlagsText(h.Flags));
            p.Field("AttenuationConstant", h.AttenuationConstant);
            p.Field("AttenuationLinear", h.AttenuationLinear);
            p.Field("AttenuationQuadratic", h.AttenuationQuadratic);
            p.Field("Range", h.Range);
            p.Field("SpotInnerAngle", h.SpotInnerAngle);
            p.Field("SpotOuterAngle", h.SpotOuterAngle);
            p.Field("ShadowResolution", int64_t(h.ShadowResolution));
            p.Field("ShadowConstantBias", h.ShadowConstantBias);
            p.Field("ShadowSlopeScaleBias", h.ShadowSlopeScaleBias);
            p.Field("ShadowNormalOffset", h.ShadowNormalOffset);
            break;
        }
        case AssetType::Placement:
        {
            CPayloadPrinter<CpkgPlacementData> p(payload, out, 4);
            const auto &h = p.Header("CpkgPlacementData");
            p.Field("NodeIndex", int64_t(h.NodeIndex));
            p.Field("Mobility", EnumText(h.Mobility, MobilityName));
            p.String("Bundle", h.Bundle);
            p.Transform("Transform", h.Transform);
            break;
        }
        case AssetType::Skybox:
        {
            CPayloadPrinter<CpkgSkyboxData> p(payload, out, 4);
            const auto &h = p.Header("CpkgSkyboxData");
            p.String("Image", h.Image);
            p.Field("Orientation", h.Orientation);
            p.Field("Intensity", h.Intensity);
            break;
        }
        case AssetType::WorldSettings:
        {
            CPayloadPrinter<CpkgWorldSettingsData> p(payload, out, 4);
            const auto &h = p.Header("CpkgWorldSettingsData");
            p.Elements("Regions", h.Regions, [&p](const CpkgWorldRegionRef &region)
            {
                p.String("Region", region.Region);
                p.Bounds("StreamingBounds", region.StreamingBounds);
            });
            p.Strings("Skyboxes", h.Skyboxes);
            p.Field("DefaultSkybox", int64_t(h.DefaultSkybox));
            p.Field("ActiveCameraIndex", int64_t(h.ActiveCameraIndex));
            p.Field("BackgroundColor", h.BackgroundColor);
            break;
        }
        case AssetType::MeshInstance:
        {
            CPayloadPrinter<CpkgMeshInstanceData> p(payload, out, 4);
            const auto &h = p.Header("CpkgMeshInstanceData");
            p.Field("NodeIndex", int64_t(h.NodeIndex));
            p.Field("SkeletonInstanceIndex", int64_t(h.SkeletonInstanceIndex));
            p.String("MeshData", h.MeshData);
            p.String("Skin", h.Skin);
            break;
        }
        case AssetType::ImportNode:
        {
            CPayloadPrinter<CpkgImportNodeData> p(payload, out, 4);
            const auto &h = p.Header("CpkgImportNodeData");
            p.Field("PlacementIndex", int64_t(h.PlacementIndex));
            p.String("ExportName", h.ExportName);
            p.Transform("LocalTransform", h.LocalTransform);
            break;
        }
        case AssetType::ExportList:
        {
            CPayloadPrinter<CpkgExportListData> p(payload, out, 4);
            const auto &h = p.Header("CpkgExportListData");
            std::string indices;
            for (const int32_t index : p.Resolve(h.NodeIndices, "NodeIndices"))
            {
                if (!indices.empty())
                    indices += ", ";
                indices += std::to_string(index);
            }
            p.Field("NodeIndices", indices);
            break;
        }
        case AssetType::Skeleton:
        {
            CPayloadPrinter<CpkgSkeletonData> p(payload, out, 4);
            const auto &h = p.Header("CpkgSkeletonData");
            p.Elements("Bones", h.Bones, [&p](const CpkgSkeletonBone &bone)
            {
                p.Field("ParentIndex", int64_t(bone.ParentIndex));
                p.String("Name", bone.Name);
                p.Transform("RestTransform", bone.RestTransform);
            });
            break;
        }
        case AssetType::SkeletonInstance:
        {
            CPayloadPrinter<CpkgSkeletonInstanceData> p(payload, out, 4);
            const auto &h = p.Header("CpkgSkeletonInstanceData");
            p.Field("NodeIndex", int64_t(h.NodeIndex));
            p.String("Skeleton", h.Skeleton);
            break;
        }
        case AssetType::SkeletalAnimation:
        {
            CPayloadPrinter<CpkgSkeletalAnimationData> p(payload, out, 4);
            const auto &h = p.Header("CpkgSkeletalAnimationData");
            p.String("Skeleton", h.Skeleton);
            p.Elements("Tracks", h.Tracks, [&p](const CpkgBoneTrack &track)
            {
                p.String("Bone", track.Bone);
                p.Field("Property", EnumText(track.Property, PropertyName));
                p.Count("Times", track.Times);
                p.Count("Values", track.Values);
                p.Count("Interpolations", track.Interpolations);
                p.Count("Tangents", track.Tangents);
            });
            break;
        }
        case AssetType::MeshField:
        {
            CPayloadPrinter<CpkgMeshFieldData> p(payload, out, 4);
            const auto &h = p.Header("CpkgMeshFieldData");
            p.Field("NodeIndex", int64_t(h.NodeIndex));
            p.Field("CullDistance", h.CullDistance);
            p.String("MeshData", h.MeshData);
            p.Count("Transforms", h.Transforms);
            break;
        }
        default:
            break;
        }
    }

    //--------------------------------------------------------------------------------------------
    // Files

    // A payload buffer aligned for the payload helpers.
    struct alignas(kCpkgPayloadAlignment) PayloadBlock
    {
        std::byte Bytes[kCpkgPayloadAlignment];
    };

    size_t Digits(uint64_t value)
    {
        return std::to_string(value).size();
    }

    void PrintBundle(const CCpkgReader &reader, uint32_t bundleIndex, const DumpOptions &options, std::ostream &out, CDiagnostics &diagnostics)
    {
        const uint32_t assetCount = reader.GetAssetCount(bundleIndex);
        out << "\nBundle " << bundleIndex << ": " << Quoted(reader.GetBundleName(bundleIndex)) << " (" << Plural(assetCount, "asset") << ")\n";

        size_t typeWidth = 0;
        size_t sizeWidth = 0;
        for (uint32_t i = 0; i < assetCount; ++i)
        {
            typeWidth = std::max(typeWidth, std::string_view(AssetTypeName(reader.GetAssetType(bundleIndex, i))).size());
            sizeWidth = std::max(sizeWidth, Digits(reader.GetAssetCompression(bundleIndex, i).RawSize));
        }
        const size_t indexWidth = assetCount > 0 ? Digits(assetCount - 1) : 1;

        std::vector<PayloadBlock> buffer;
        for (uint32_t i = 0; i < assetCount; ++i)
        {
            const AssetType type = reader.GetAssetType(bundleIndex, i);
            const std::string typeName = AssetTypeName(type);
            const uint64_t size = reader.GetAssetCompression(bundleIndex, i).RawSize;
            const std::string_view name = reader.GetAssetName(bundleIndex, i);

            const std::string index = std::to_string(i);
            const std::string sizeText = std::to_string(size);
            out << "  " << std::string(indexWidth - index.size(), ' ') << index << "  " << typeName
                << std::string(typeWidth - typeName.size(), ' ') << "  " << std::string(sizeWidth - sizeText.size(), ' ') << sizeText << " bytes";
            if (!name.empty())
                out << "  " << Quoted(name);
            out << '\n';

            if (!options.Payloads)
                continue;

            auto fail = [&](const std::string &message)
            {
                diagnostics.Report(Severity::Error, options.Package,
                    "bundle " + Quoted(reader.GetBundleName(bundleIndex)) + ", asset " + index + " (" + typeName + "): " + message);
            };

            try
            {
                buffer.assign(size_t((size + kCpkgPayloadAlignment - 1) / kCpkgPayloadAlignment), PayloadBlock{});
            }
            catch (const std::bad_alloc &)
            {
                fail("the payload is too large to read");
                continue;
            }
            catch (const std::length_error &)
            {
                fail("the payload is too large to read");
                continue;
            }
            const std::span<std::byte> payload(reinterpret_cast<std::byte *>(buffer.data()), size_t(size));
            if (size > 0 && Gem::Failed(reader.ReadAsset(bundleIndex, i, payload)))
            {
                fail("the payload could not be read");
                continue;
            }

            std::ostringstream fields;
            try
            {
                PrintPayload(type, payload, fields);
                out << fields.str();
            }
            catch (const PayloadError &e)
            {
                fail(e.Message);
            }
        }
    }
}

//------------------------------------------------------------------------------------------------
const char *AssetTypeName(AssetType type)
{
    switch (type)
    {
    case AssetType::TransformNode:      return "TransformNode";
    case AssetType::MeshData:           return "MeshData";
    case AssetType::Material:           return "Material";
    case AssetType::Image:              return "Image";
    case AssetType::AnimationClip:      return "AnimationClip";
    case AssetType::MeshSkin:           return "MeshSkin";
    case AssetType::Camera:             return "Camera";
    case AssetType::Light:              return "Light";
    case AssetType::Placement:          return "Placement";
    case AssetType::Skybox:             return "Skybox";
    case AssetType::WorldSettings:      return "WorldSettings";
    case AssetType::MeshInstance:       return "MeshInstance";
    case AssetType::ImportNode:         return "ImportNode";
    case AssetType::ExportList:         return "ExportList";
    case AssetType::Skeleton:           return "Skeleton";
    case AssetType::SkeletonInstance:   return "SkeletonInstance";
    case AssetType::SkeletalAnimation:  return "SkeletalAnimation";
    case AssetType::MeshField:          return "MeshField";
    default:                            return nullptr;
    }
}

std::string OpenFailure(Gem::Result result, const std::string &package)
{
    switch (result)
    {
    case Gem::Result::NotFound:
        return "file not found";
    case Gem::Result::DataNotRecognized:
        return "not a package file";
    case Gem::Result::CorruptedData:
        return "the package file is damaged";
    case Gem::Result::OutOfRange:
    {
        uint32_t version = 0;
        const std::string text = ReadVersion(package, &version)
            ? "the package's format version " + std::to_string(version) + " is not supported"
            : std::string("the package's format version is not supported");
        return text + "; this cpkg reads versions " + std::to_string(kMinSupportedVersion) + " to " + std::to_string(kMaxSupportedVersion);
    }
    case Gem::Result::OutOfMemory:
        return "out of memory";
    default:
        return "the file could not be read";
    }
}

void Dump(const DumpOptions &options, std::ostream &out, CDiagnostics &diagnostics)
{
    CCpkgReader reader;
    const Gem::Result result = reader.Open(options.Package.c_str());
    if (Gem::Failed(result))
    {
        diagnostics.Report(Severity::Error, options.Package, OpenFailure(result, options.Package));
        return;
    }

    uint32_t onlyBundle = UINT32_MAX;
    if (options.Bundle)
    {
        for (uint32_t i = 0; i < reader.GetBundleCount(); ++i)
        {
            if (reader.GetBundleName(i) == *options.Bundle)
            {
                onlyBundle = i;
                break;
            }
        }
        if (onlyBundle == UINT32_MAX)
        {
            diagnostics.Report(Severity::Error, options.Package, "the package has no bundle named " + Quoted(*options.Bundle));
            return;
        }
    }

    out << "Package: " << Quoted(reader.GetPackageName()) << '\n';
    out << "Version: " << reader.GetVersion() << '\n';
    out << "Bundles: " << reader.GetBundleCount() << '\n';

    for (uint32_t i = 0; i < reader.GetBundleCount(); ++i)
    {
        if (onlyBundle == UINT32_MAX || onlyBundle == i)
            PrintBundle(reader, i, options, out, diagnostics);
    }
}

} // namespace CpkgTool
