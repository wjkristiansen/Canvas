//================================================================================================
// Source - a package source as cpkg's build reads it
//
// LoadPackageSource is the build's Load stage: it reads a package file and the bundle files it
// includes, checks each file against the package source format (src/cpkg/PackageSource.md and
// package-source.schema.json), and holds what they declare in the structures below. It also
// reads every binary array file, so each array holds its elements whichever form it was written
// in. Fields whose omission means something other than a fixed default, such as a computed bounds
// or a field an edit leaves unchanged, are std::optional; other omitted fields hold their default.
//
// Every value keeps its SourceLocation, so later stages report what they find at the value.
//================================================================================================

#pragma once

#include "CanvasPkg.h"
#include "Diagnostics.h"
#include "JsonText.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CpkgTool
{

using Json = nlohmann::ordered_json;

// One JSON file of a package source.
struct SourceFile
{
    std::string Path;                               // UTF-8, as the build resolved it
    std::string Text;
    Json Document;
    std::unique_ptr<Canvas::CJsonLineIndex> Lines;  // over Text
};

// Where a value is: its file, its byte offset, and its JSON Pointer. A map key's location is
// the key's opening quote, with the pointer of the member it names.
struct SourceLocation
{
    uint32_t File = 0;      // index in SourcePackage::Files
    size_t Offset = 0;
    std::string Pointer;
};

// A string value: a name, a path, or a bone reference.
struct SourceString
{
    std::string Text;
    SourceLocation Where;
};

// A field naming a file on disk.
struct SourceFileRef
{
    std::string Written;    // as the source writes it
    std::string Path;       // resolved against the directory of the JSON file holding it
    SourceLocation Where;
};

// The range of a file that holds a binary array, as the source writes it.
struct SourceArrayFile
{
    SourceFileRef Ref;
    std::optional<uint64_t> Offset;     // in bytes; the start of the file when absent
    std::optional<uint32_t> Count;      // in elements; the rest of the file when absent
};

// A large numeric array, written inline or as { "file": file }. Values holds the elements'
// components in order, so its size is a multiple of the element's component count.
template <class T>
struct SourceArray
{
    std::vector<T> Values;
    std::optional<SourceArrayFile> File;    // the binary array's file, when written { "file" }
    SourceLocation Where;
};

using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;

// A rotation as a quaternion, XYZW, converted from the degrees form when written that way. Not
// yet normalized; Validate checks that it is non-zero.
struct SourceRotation
{
    Vec4 Quaternion = { 0.0f, 0.0f, 0.0f, 1.0f };
    SourceLocation Where;
};

// Omitted parts are the identity, or unchanged for an edit.
struct SourceTransform
{
    std::optional<Vec3> Translation;
    std::optional<SourceRotation> Rotation;
    std::optional<Vec3> Scale;
};

struct SourceBounds
{
    Vec3 Min = {};
    Vec3 Max = {};
    SourceLocation Where;
};

template <class T>
struct SourceNamed
{
    SourceString Name;      // the map key
    T Value;
};

//------------------------------------------------------------------------------------------------
// Shared assets

enum class SourceImageForm
{
    File,
    Cubemap,
    General,
};

struct SourceImage
{
    SourceImageForm Form = SourceImageForm::File;
    Canvas::CpkgImageDimension Dimension = Canvas::CpkgImageDimension::Texture2D;
    uint32_t ArraySize = 1;
    uint32_t MipLevels = 1;
    std::vector<SourceFileRef> Files;   // a cubemap's in +X, -X, +Y, -Y, +Z, -Z order
    SourceLocation Where;
};

// Texture roles, in CpkgMaterialData order.
enum class SourceTextureRole
{
    Albedo,
    Normal,
    Roughness,
    Metallic,
    AmbientOcclusion,
    Emissive,
    Count,
};

struct SourceDisplacement
{
    SourceString Map;
    float Scale = 1.0f;
    float Bias = 0.0f;
    float MinTessFactor = 2.0f;
    float MaxTessFactor = 32.0f;
    float DistanceLodScale = 10.0f;
    float CurvatureLodScale = 0.5f;
    SourceLocation Where;
};

struct SourceMaterial
{
    Vec4 BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vec3 Emissive = { 0.0f, 0.0f, 0.0f };
    float Roughness = 1.0f;
    float Metallic = 0.0f;
    float AmbientOcclusion = 1.0f;
    std::array<std::optional<SourceString>, size_t(SourceTextureRole::Count)> Textures;
    std::optional<SourceDisplacement> Displacement;
    SourceLocation Where;
};

struct SourceMeshGroup
{
    std::optional<SourceString> Material;
    SourceArray<float> Positions;                       // 3 per vertex
    std::optional<SourceArray<float>> Normals;          // 3 per vertex
    std::optional<SourceArray<float>> UV0;              // 2 per vertex
    std::optional<SourceArray<float>> Tangents;         // 4 per vertex
    std::optional<SourceArray<uint32_t>> BoneIndices;   // 4 per vertex
    std::optional<SourceArray<float>> BoneWeights;      // 4 per vertex
    SourceLocation Where;
};

enum class SourceBoundsForm
{
    Computed,   // omitted
    Declared,
    None,       // "none": the empty sentinel
};

struct SourceMesh
{
    Canvas::GfxPrimitiveTopology Topology = Canvas::GfxPrimitiveTopology::TriangleList;
    SourceBoundsForm BoundsForm = SourceBoundsForm::Computed;
    SourceBounds Bounds;                                // when Declared
    std::vector<SourceMeshGroup> Groups;
    SourceLocation Where;
};

struct SourceBone
{
    SourceString Name;
    std::optional<SourceString> Parent;
    SourceTransform Rest;
    SourceLocation Where;
};

struct SourceSkeleton
{
    std::vector<SourceBone> Bones;
    SourceLocation Where;
};

struct SourceSkin
{
    SourceString Skeleton;
    std::vector<SourceString> Bones;
    std::vector<SourceTransform> BindPoses;             // one per bone, in the mesh's space
    SourceLocation BindPosesWhere;
    SourceLocation Where;
};

// A track of a skeletal animation, whose Target is a bone, or of a clip, whose Target is a node.
struct SourceTrack
{
    SourceString Target;
    Canvas::CpkgAnimationProperty Property = Canvas::CpkgAnimationProperty::Translation;
    SourceArray<float> Times;
    SourceArray<float> Values;
    std::vector<Canvas::AnimationInterpolation> Interpolations;  // one for every key, or one per key
    bool InterpolationPerKey = false;                            // written as an array
    SourceLocation InterpolationWhere;                           // when written
    std::optional<SourceArray<float>> Tangents;
    SourceLocation Where;
};

struct SourceSkeletalAnimation
{
    SourceString Skeleton;
    std::vector<SourceTrack> Tracks;
    SourceLocation Where;
};

struct SourceSkybox
{
    SourceString Image;
    std::optional<SourceRotation> Orientation;
    float Intensity = 1.0f;
    SourceLocation Where;
};

//------------------------------------------------------------------------------------------------
// Bundle structure

struct SourceSkeletonInstance
{
    SourceString Skeleton;
    SourceLocation Where;
};

struct SourceMeshInstance
{
    SourceString Mesh;
    std::optional<SourceString> Skin;
    std::optional<SourceString> SkeletonInstance;   // a node name
    SourceLocation Where;
};

struct SourceMeshField
{
    std::optional<SourceString> Name;
    SourceString Mesh;
    float CullDistance = 0.0f;
    std::vector<SourceTransform> Transforms;
    SourceLocation Where;
};

// Omitted fields take CpkgCameraData's defaults, or stay unchanged for an edit.
struct SourceCamera
{
    std::optional<float> NearClip;
    std::optional<float> FarClip;
    std::optional<float> FovAngle;
    std::optional<float> ExposureStops;
    SourceLocation Where;
};

// Omitted fields take CpkgLightData's defaults, or stay unchanged for an edit. Type is required
// except in an edit of a node that has a light.
struct SourceLight
{
    std::optional<Canvas::LightType> Type;
    std::optional<Vec3> Color;
    std::optional<float> Intensity;
    std::optional<bool> Enabled;
    std::optional<bool> CastsShadows;
    std::optional<float> Range;
    std::optional<float> AttenuationConstant;
    std::optional<float> AttenuationLinear;
    std::optional<float> AttenuationQuadratic;
    std::optional<float> SpotInnerAngle;
    std::optional<float> SpotOuterAngle;
    std::optional<uint32_t> ShadowResolution;
    std::optional<float> ShadowConstantBias;
    std::optional<float> ShadowSlopeScaleBias;
    std::optional<float> ShadowNormalOffset;
    SourceLocation Where;
};

struct SourcePlacement
{
    std::optional<SourceString> Name;
    SourceString Bundle;
    std::optional<Canvas::CpkgMobility> Mobility;
    SourceTransform Transform;
    SourceLocation Where;
};

struct SourceNode;

// What a node, an import node, or an edit attaches. An edit holds no skeleton instance.
struct SourceAttachments
{
    std::optional<SourceSkeletonInstance> SkeletonInstance;
    std::vector<SourceMeshInstance> MeshInstances;
    std::vector<SourceMeshField> MeshFields;
    std::optional<SourceCamera> Camera;
    std::optional<SourceLight> Light;
    std::vector<SourcePlacement> Placements;
    std::vector<SourceNode> Children;
};

struct SourceNode
{
    std::optional<SourceString> Name;
    SourceTransform Transform;
    std::optional<Canvas::CpkgMobility> Mobility;
    std::optional<SourceString> Bone;
    SourceAttachments Attachments;
    SourceLocation Where;
};

struct SourceImportNode
{
    std::optional<SourceString> Name;
    SourceString Placement;
    SourceString Export;
    SourceTransform Transform;
    SourceAttachments Attachments;
    SourceLocation Where;
};

struct SourceClipBinding
{
    SourceString SkeletonInstance;  // a node name
    SourceString Animation;
    SourceLocation Where;
};

struct SourceClip
{
    std::optional<float> Duration;
    std::vector<SourceTrack> Tracks;
    std::vector<SourceClipBinding> SkeletalAnimations;
    SourceLocation Where;
};

struct SourceRegion
{
    SourceString Region;
    SourceBounds Bounds;
    SourceLocation Where;
};

struct SourceWorld
{
    std::vector<SourceRegion> Regions;
    std::vector<SourceString> Skyboxes;
    std::optional<SourceString> DefaultSkybox;
    std::optional<SourceString> ActiveCamera;   // a node name
    Vec4 BackgroundColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    SourceLocation Where;
};

//------------------------------------------------------------------------------------------------
// Imports

// The kinds of asset an import builds, as "names" and "rename" key them.
enum class SourceImportKind
{
    Images,
    Materials,
    Meshes,
    Skeletons,
    Skins,
    SkeletalAnimations,
    Clips,
    Nodes,
    Count,
};

struct SourceImportClip
{
    SourceString Name;
    std::optional<SourceString> Stack;
    std::optional<float> Start;
    std::optional<float> End;
    std::optional<std::array<uint32_t, 2>> Frames;
    SourceLocation Where;
};

struct SourceNodeEdit
{
    SourceTransform Transform;
    std::optional<Canvas::CpkgMobility> Mobility;
    SourceAttachments Attachments;
    SourceLocation Where;
};

// A map from a name to a string: rename's built names, or replace's asset paths.
using SourceStringMap = std::vector<std::pair<SourceString, SourceString>>;

struct SourceImport
{
    SourceFileRef File;
    float Scale = 1.0f;
    std::optional<SourceRotation> Rotation;
    bool Nodes = true;
    std::optional<SourceString> Parent;
    std::optional<SourceString> Skeleton;
    std::array<std::optional<SourceString>, size_t(SourceImportKind::Count)> Names;
    std::array<SourceStringMap, size_t(SourceImportKind::Count)> Rename;
    SourceStringMap ReplaceImages;
    SourceStringMap ReplaceMaterials;
    std::optional<std::vector<SourceImportClip>> Clips;
    std::vector<SourceNamed<SourceNodeEdit>> Edits;
    SourceLocation Where;
};

//------------------------------------------------------------------------------------------------
// Bundles and packages

struct SourceBundle
{
    SourceString Name;
    std::vector<SourceNamed<SourceImage>> Images;
    std::vector<SourceNamed<SourceMaterial>> Materials;
    std::vector<SourceNamed<SourceMesh>> Meshes;
    std::vector<SourceNamed<SourceSkeleton>> Skeletons;
    std::vector<SourceNamed<SourceSkin>> Skins;
    std::vector<SourceNamed<SourceSkeletalAnimation>> SkeletalAnimations;
    std::vector<SourceNamed<SourceSkybox>> Skyboxes;
    std::vector<SourceNode> Nodes;
    std::vector<SourcePlacement> Placements;
    std::vector<SourceMeshField> MeshFields;
    std::vector<SourceImportNode> ImportNodes;
    std::vector<SourceNamed<SourceClip>> Clips;
    std::optional<std::vector<SourceString>> Exports;
    std::optional<SourceWorld> World;
    std::vector<SourceImport> Imports;
    SourceLocation Where;
};

struct SourcePackage
{
    std::vector<std::unique_ptr<SourceFile>> Files;     // the package file first, then each bundle file
    SourceString PackageName;
    std::vector<SourceBundle> Bundles;                  // in the package file's order
};

//------------------------------------------------------------------------------------------------
// Names a source writes for values

// The name a source writes for an enumerator.
template <class T>
struct EnumName
{
    std::string_view Name;
    T Value;
};

inline constexpr EnumName<Canvas::CpkgMobility> kMobilities[] = {
    { "static", Canvas::CpkgMobility::Static },
    { "stationary", Canvas::CpkgMobility::Stationary },
    { "movable", Canvas::CpkgMobility::Movable },
};

inline constexpr EnumName<Canvas::GfxPrimitiveTopology> kTopologies[] = {
    { "triangleList", Canvas::GfxPrimitiveTopology::TriangleList },
    { "patchList4CP", Canvas::GfxPrimitiveTopology::PatchList4CP },
};

inline constexpr EnumName<Canvas::CpkgImageDimension> kDimensions[] = {
    { "texture1D", Canvas::CpkgImageDimension::Texture1D },
    { "texture2D", Canvas::CpkgImageDimension::Texture2D },
    { "texture3D", Canvas::CpkgImageDimension::Texture3D },
    { "cube", Canvas::CpkgImageDimension::Cube },
};

inline constexpr EnumName<Canvas::CpkgAnimationProperty> kProperties[] = {
    { "translation", Canvas::CpkgAnimationProperty::Translation },
    { "rotation", Canvas::CpkgAnimationProperty::Rotation },
    { "scale", Canvas::CpkgAnimationProperty::Scale },
};

inline constexpr EnumName<Canvas::AnimationInterpolation> kInterpolations[] = {
    { "linear", Canvas::AnimationInterpolation::Linear },
    { "step", Canvas::AnimationInterpolation::Step },
    { "cubic", Canvas::AnimationInterpolation::Cubic },
};

inline constexpr EnumName<Canvas::LightType> kLightTypes[] = {
    { "ambient", Canvas::LightType::Ambient },
    { "point", Canvas::LightType::Point },
    { "directional", Canvas::LightType::Directional },
    { "spot", Canvas::LightType::Spot },
    { "area", Canvas::LightType::Area },
};

// Keys of a material's "textures", in SourceTextureRole order.
inline constexpr const char *kTextureRoles[] = { "albedo", "normal", "roughness", "metallic", "ambientOcclusion", "emissive" };
static_assert(std::size(kTextureRoles) == size_t(SourceTextureRole::Count));

// Keys of a cubemap, in SourceImage::Files order.
inline constexpr const char *kCubeFaces[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };

// The name names gives value, or an empty name when it gives none.
template <class T, size_t N>
std::string_view EnumNameOf(const EnumName<T> (&names)[N], T value)
{
    for (const EnumName<T> &name : names)
    {
        if (name.Value == value)
            return name.Name;
    }
    return {};
}

// Reads the package file at packageFile, a UTF-8 path, and the bundle files it includes into
// *pPackage, and reports what does not follow the package source format, in document order
// within each file. Returns true when it reported no error; *pPackage then holds the whole
// source, and otherwise what could be read.
bool LoadPackageSource(std::string_view packageFile, SourcePackage *pPackage, CDiagnostics &diagnostics);

// Reports a finding at a value of the package source.
void ReportAt(CDiagnostics &diagnostics, const SourcePackage &package, Severity severity, const SourceLocation &where, std::string_view message);

} // namespace CpkgTool
