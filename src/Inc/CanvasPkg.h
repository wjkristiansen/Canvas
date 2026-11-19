//================================================================================================
// CanvasPkg
//
// The CanvasPkg package format: every on-disk structure of a .cpkg file and the header-only
// helpers that encode and decode asset payloads. See src/CanvasPkg/README.md.
//
// Every on-disk structure is a struct declared here, and its on-disk form is its sizeof(T)
// in-memory bytes. Each struct declares every byte it holds, so the static_asserts below fail
// to compile on any compiler that would insert implicit padding.
//
// Canvas uses row vectors throughout; see "Matrix and Quaternion Conventions" in the root README.
//================================================================================================

#pragma once

#include <Gem.hpp>
#include "CanvasCore.h"
#include "CanvasGfx.h"

#include <bit>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static_assert(std::endian::native == std::endian::little, "CanvasPkg supports only little-endian hosts");
static_assert(alignof(uint64_t) == 8, "CanvasPkg requires 8-byte alignment of 64-bit members");

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Format versions

constexpr uint32_t kMinSupportedVersion = 1;  // oldest CpkgFileHeader::Version this build can still read
constexpr uint32_t kMaxSupportedVersion = 1;  // current format version this build implements

// A payload loaded into a buffer aligned to this can be accessed in place.
constexpr size_t kCpkgPayloadAlignment = 16;

//------------------------------------------------------------------------------------------------
// Count elements of T, stored contiguously in the data area of the payload that contains this
// span.
template <class T>
struct CpkgSpan
{
    uint64_t Offset = 0;  // byte offset of element 0 from the start of the payload
    uint64_t Count  = 0;  // number of elements; 0 = empty
};

//------------------------------------------------------------------------------------------------
enum class AssetType : uint32_t
{
    Unknown           = 0,   // sentinel; never assigned to a real asset
    TransformNode     = 1,
    MeshData          = 2,
    Material          = 3,
    Image             = 4,
    AnimationClip     = 5,   // see CpkgAnimationClipData
    MeshSkin          = 6,
    Camera            = 7,
    Light             = 8,
    Placement         = 9,   // see Placements
    Skybox            = 10,
    WorldSettings     = 11,  // see Worlds
    MeshInstance      = 12,
    ImportNode        = 13,  // see Exports and Imports
    ExportList        = 14,  // see Exports and Imports
    Skeleton          = 15,  // see CpkgSkeletonData
    SkeletonInstance  = 16,
    SkeletalAnimation = 17,  // see CpkgSkeletalAnimationData
    MeshField         = 18,  // see CpkgMeshFieldData
};

// True for the shared asset types; false for the local ones and for Unknown.
constexpr bool CpkgIsSharedAssetType(AssetType type)
{
    switch (type)
    {
    case AssetType::Image:
    case AssetType::Material:
    case AssetType::MeshData:
    case AssetType::MeshSkin:
    case AssetType::Skeleton:
    case AssetType::SkeletalAnimation:
    case AssetType::Skybox:
        return true;
    default:
        return false;
    }
}

//------------------------------------------------------------------------------------------------
// Compression

enum class CpkgCodec : uint32_t
{
    None = 0,
};

// Stored in the on-disk CpkgAssetTableEntry.
struct CpkgCompressionScheme
{
    CpkgCodec Codec      = CpkgCodec::None;  // codec the payload is stored with
    uint32_t  Padding    = 0;
    uint64_t  StoredSize = 0;   // on-disk byte count
    uint64_t  RawSize    = 0;   // uncompressed byte count
};

//------------------------------------------------------------------------------------------------
// File structure

// First bytes of a .cpkg file.
struct CpkgFileHeader
{
    char     Magic[4];              // "CPKG"; CCpkgReader::Open fails if this doesn't match
    uint32_t Version;               // format version; CCpkgReader::Open fails if outside [kMinSupportedVersion, kMaxSupportedVersion]
    uint64_t BundleTableOffset;     // file offset of the file-level bundle table
    uint64_t BundleNameBlobOffset;  // file offset of the bundle name blob
    uint64_t BundleNameBlobSize;    // byte size of the bundle name blob
    uint64_t PackageNameOffset;     // file offset of the package name
    uint32_t BundleCount;           // number of entries at BundleTableOffset
    uint32_t PackageNameLength;     // byte length of the package name; never 0
};

// One entry per bundle in the file-level bundle table.
struct CpkgBundleTableEntry
{
    uint64_t AssetTableOffset;      // file offset of this bundle's asset table
    uint64_t NameOffset;            // byte offset of this bundle's name within the bundle name blob
    uint64_t AssetNameBlobOffset;   // file offset of this bundle's asset name blob
    uint64_t AssetNameBlobSize;     // byte size of this bundle's asset name blob
    uint32_t AssetCount;            // number of entries at AssetTableOffset
    uint32_t NameLength;            // byte length of this bundle's name
};

// One entry per asset in a bundle's asset table.
struct CpkgAssetTableEntry
{
    uint64_t               PayloadOffset; // file offset of this asset's payload
    uint64_t               NameOffset;    // byte offset of this asset's name within the bundle's asset name blob
    AssetType              Type;
    uint32_t               NameLength;    // byte length of this asset's name; 0 = unnamed, and NameOffset is then 0
    CpkgCompressionScheme  Compression;
};

//------------------------------------------------------------------------------------------------
// Shared value types

struct CpkgTransform
{
    float Scale[3]       = { 1.0f, 1.0f, 1.0f };        // xyz
    float Rotation[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };  // unit quaternion, XYZW
    float Translation[3] = { 0.0f, 0.0f, 0.0f };        // xyz, meters
};

struct CpkgAABB
{
    float Min[3] = {  FLT_MAX,  FLT_MAX,  FLT_MAX };  // xyz
    float Max[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };  // xyz
};

enum class CpkgMobility : uint32_t
{
    Static     = 0,   // transform and attributes are fixed
    Stationary = 1,   // transform is fixed; attributes may change
    Movable    = 2,   // transform and attributes may change
};

//------------------------------------------------------------------------------------------------
// Bundle structure

struct CpkgPlacementData
{
    int32_t                  NodeIndex = -1;                   // asset index of the node this placement attaches to; -1 = instance transform
    CpkgMobility             Mobility  = CpkgMobility::Static; // mobility of the nested instance's instance transform
    CpkgSpan<char>           Bundle;                           // bundle path to the placed bundle; required
    CpkgTransform            Transform;                        // relative to NodeIndex
};

struct CpkgWorldRegionRef
{
    CpkgSpan<char>  Region;           // bundle path to the region's bundle; required
    CpkgAABB        StreamingBounds;  // world space; Min[i] <= Max[i] on every axis
};

struct CpkgWorldSettingsData
{
    CpkgSpan<CpkgWorldRegionRef>  Regions;
    CpkgSpan<CpkgSpan<char>>      Skyboxes;                    // asset paths to Skybox assets; each required
    int32_t                       DefaultSkybox         = -1;  // position in Skyboxes (not an asset index); -1 = none
    int32_t                       ActiveCameraIndex     = -1;  // asset index of a Camera in this bundle; -1 = none
    float                         BackgroundColor[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };  // linear RGBA; the background while no skybox is in effect
};

struct CpkgTransformNodeData
{
    int32_t         ParentIndex = -1;                    // asset index of the parent node or SkeletonInstance; -1 = instance transform
    CpkgMobility    Mobility    = CpkgMobility::Static;  // inherited by descendants
    CpkgSpan<char>  ParentBone;                          // name of the parent bone when ParentIndex is a SkeletonInstance; otherwise empty
    CpkgTransform   LocalTransform;                      // relative to the parent node or bone
};

struct CpkgImportNodeData
{
    int32_t         PlacementIndex = -1;  // asset index of the Placement whose nested instance exports the node; required
    uint32_t        Padding        = 0;
    CpkgSpan<char>  ExportName;           // export name in the placed bundle's ExportList; required
    CpkgTransform   LocalTransform;       // relative to the exported node
};

struct CpkgExportListData
{
    CpkgSpan<int32_t>  NodeIndices;  // asset indices of exported nodes; each named
};

//------------------------------------------------------------------------------------------------
// Skeletons

struct CpkgSkeletonBone
{
    int32_t         ParentIndex = -1;  // bone index of the parent bone; -1 = root
    uint32_t        Padding     = 0;
    CpkgSpan<char>  Name;              // required; unique within the skeleton
    CpkgTransform   RestTransform;     // relative to the parent bone, or for a root to the skeleton instance's node
};

struct CpkgSkeletonData
{
    CpkgSpan<CpkgSkeletonBone>  Bones;  // at least one
};

struct CpkgSkeletonInstanceData
{
    int32_t         NodeIndex = -1;  // asset index of the node this skeleton instance attaches to; -1 = instance transform
    uint32_t        Padding   = 0;
    CpkgSpan<char>  Skeleton;        // asset path to a Skeleton; required
};

//------------------------------------------------------------------------------------------------
// Mesh attachments

struct CpkgMeshInstanceData
{
    int32_t         NodeIndex             = -1;  // asset index of the node this mesh instance attaches to; required
    int32_t         SkeletonInstanceIndex = -1;  // asset index of the SkeletonInstance that poses the skin; -1 = not skinned
    CpkgSpan<char>  MeshData;                    // asset path to a MeshData; required
    CpkgSpan<char>  Skin;                        // asset path to a MeshSkin; null = not skinned
};

struct CpkgMeshFieldData
{
    int32_t                  NodeIndex    = -1;    // asset index of the node this mesh field attaches to; -1 = instance transform
    float                    CullDistance = 0.0f;  // meters from the camera beyond which entries are not drawn; 0 = no limit
    CpkgSpan<char>           MeshData;             // asset path to a MeshData; required
    CpkgSpan<CpkgTransform>  Transforms;           // one drawn copy per entry, relative to NodeIndex; at least one
};

//------------------------------------------------------------------------------------------------
// Mesh data

struct CpkgVertexPosition    { float    XYZ[3];    };
struct CpkgVertexNormal      { float    XYZ[3];    };
struct CpkgVertexUV          { float    UV[2];     };
struct CpkgVertexTangent     { float    XYZW[4];   };  // xyz = tangent, w = bitangent sign
struct CpkgVertexBoneIndices { uint32_t Index[4];  };
struct CpkgVertexBoneWeights { float    Weight[4]; };

// One material partition of a mesh. The vertex count is Positions.Count.
struct CpkgMeshDataGroup
{
    CpkgSpan<char>                  Material;            // asset path to a Material; empty = none
    CpkgSpan<CpkgVertexPosition>    Positions;           // required by every topology
    CpkgSpan<CpkgVertexNormal>      Normals;             // required by every topology
    CpkgSpan<CpkgVertexUV>          UV0;                 // required by PatchList4CP, optional otherwise
    CpkgSpan<CpkgVertexTangent>     Tangents;            // optional
    CpkgSpan<CpkgVertexBoneIndices> BoneIndices;         // optional; bones of the mesh's skin
    CpkgSpan<CpkgVertexBoneWeights> BoneWeights;         // optional
};

struct CpkgMeshData
{
    GfxPrimitiveTopology            Topology = GfxPrimitiveTopology::TriangleList;  // CanvasGfx.h
    CpkgAABB                        LocalBounds;         // empty sentinel = no bounds declared
    uint32_t                        Padding = 0;
    CpkgSpan<CpkgMeshDataGroup>     Groups;
};

//------------------------------------------------------------------------------------------------
// Materials

struct CpkgDisplacementData
{
    CpkgSpan<char> DisplacementMap;   // asset path to an Image; required if present
    float   MapScale          = 1.0f;
    float   MapBias           = 0.0f;
    float   MinTessFactor     = 2.0f;
    float   MaxTessFactor     = 32.0f;
    float   DistanceLodScale  = 10.0f;
    float   CurvatureLodScale = 0.5f;
};

struct CpkgMaterialData
{
    float  BaseColorFactor[4]    = { 1.0f, 1.0f, 1.0f, 1.0f };  // linear RGBA
    float  EmissiveFactor[4]     = { 0.0f, 0.0f, 0.0f, 0.0f };  // linear RGB, A unused
    float  RoughMetalAOFactor[4] = { 1.0f, 0.0f, 1.0f, 0.0f };  // R=Roughness, G=Metallic, B=AmbientOcclusion, A=spare

    CpkgSpan<char> AlbedoTexture;            // asset path to an Image; empty = none, one per MaterialLayerRole
    CpkgSpan<char> NormalTexture;
    CpkgSpan<char> RoughnessTexture;
    CpkgSpan<char> MetallicTexture;
    CpkgSpan<char> AmbientOcclusionTexture;
    CpkgSpan<char> EmissiveTexture;

    uint32_t               HasDisplacement = 0;  // 0 or 1
    uint32_t               Padding         = 0;
    CpkgDisplacementData   Displacement;         // meaningful only when HasDisplacement is 1
};

//------------------------------------------------------------------------------------------------
// Images

enum class CpkgImageEncoding : uint32_t
{
    Unknown = 0,
    Jpeg    = 1,
    Png     = 2,
    Tiff    = 3,
};

enum class CpkgImageDimension : uint32_t   // values match GfxSurfaceDimension
{
    Texture1D = 0,
    Texture2D = 1,
    Texture3D = 2,
    Cube      = 3,
};

struct CpkgImageData
{
    CpkgImageEncoding            Encoding;
    CpkgImageDimension           Dimension;
    uint32_t                     Width;      // of mip 0
    uint32_t                     Height;     // of mip 0; 1 for Texture1D
    uint32_t                     Depth;      // of mip 0; 1 unless Texture3D
    uint32_t                     ArraySize;  // array slices; a multiple of 6 for Cube; 1 for Texture3D
    uint32_t                     MipLevels;  // mip levels stored; at least 1
    uint32_t                     Padding = 0;
    CpkgSpan<CpkgSpan<uint8_t>>  Images;     // encoded 2D images, in subresource order
};

struct CpkgSkyboxData
{
    CpkgSpan<char>  Image;               // asset path to a Cube Image; required
    float           Orientation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };  // unit quaternion, XYZW: rotates cube-texture space into Canvas space
    float           Intensity  = 1.0f;   // linear multiplier on the sample
    uint32_t        Padding    = 0;
};

//------------------------------------------------------------------------------------------------
// Animation

// The property of a local transform that a track animates, and the form of its values.
enum class CpkgAnimationProperty : uint32_t
{
    Translation = 0,  // 3 floats: xyz, meters
    Rotation    = 1,  // 4 floats: a unit quaternion, XYZW
    Scale       = 2,  // 3 floats: xyz
};

struct CpkgNodeTrack
{
    int32_t                           NodeIndex = -1;  // asset index of the node this track animates; required
    CpkgAnimationProperty             Property  = CpkgAnimationProperty::Translation;
    CpkgSpan<float>                   Times;
    CpkgSpan<float>                   Values;
    CpkgSpan<AnimationInterpolation>  Interpolations;
    CpkgSpan<float>                   Tangents;
};

struct CpkgSkeletalAnimationBinding
{
    int32_t         SkeletonInstanceIndex = -1;  // asset index of the SkeletonInstance the animation poses; required
    uint32_t        Padding               = 0;
    CpkgSpan<char>  Animation;                   // asset path to a SkeletalAnimation; required
};

struct CpkgAnimationClipData
{
    float                                   Duration = 0.0f;  // seconds
    uint32_t                                Padding = 0;
    CpkgSpan<CpkgNodeTrack>                 Tracks;
    CpkgSpan<CpkgSkeletalAnimationBinding>  SkeletalAnimations;
};

struct CpkgBoneTrack
{
    CpkgSpan<char>                    Bone;               // name of the bone this track animates; required
    CpkgAnimationProperty             Property = CpkgAnimationProperty::Translation;
    uint32_t                          Padding  = 0;
    CpkgSpan<float>                   Times;
    CpkgSpan<float>                   Values;
    CpkgSpan<AnimationInterpolation>  Interpolations;
    CpkgSpan<float>                   Tangents;
};

struct CpkgSkeletalAnimationData
{
    CpkgSpan<char>           Skeleton;  // asset path to the Skeleton this animation poses; required
    CpkgSpan<CpkgBoneTrack>  Tracks;
};

//------------------------------------------------------------------------------------------------
// Skins

// Row-major, row vectors: v' = v * M, translation in row 3.
struct CpkgInvBindPose { float M[16]; };

struct CpkgMeshSkinData
{
    CpkgSpan<char>              Skeleton;      // asset path to the Skeleton this skin is built for; required
    CpkgSpan<CpkgSpan<char>>    Bones;         // per skin bone, the name of the skeleton bone it follows; at least one
    CpkgSpan<CpkgInvBindPose>   InvBindPoses;  // one per skin bone, same order as Bones
};

//------------------------------------------------------------------------------------------------
// Cameras and lights

struct CpkgCameraData
{
    int32_t NodeIndex     = -1;         // asset index of the node this camera attaches to; required
    float   NearClip      = 0.1f;
    float   FarClip       = 1000.0f;
    float   FovAngle      = 0.785398f;  // vertical field of view, radians
    float   ExposureStops = 0.0f;
};

struct CpkgLightData
{
    int32_t   NodeIndex            = -1;     // asset index of the node this light attaches to; required
    LightType Type                 = LightType::Point;
    float     Color[4]             = { 1.0f, 1.0f, 1.0f, 1.0f };  // linear RGBA
    float     Intensity            = 1.0f;
    LightFlags Flags               = LightFlags::Enabled;

    // Attenuation (Point and Spot lights)
    float     AttenuationConstant  = 1.0f;
    float     AttenuationLinear    = 0.0f;
    float     AttenuationQuadratic = 0.0f;
    float     Range                = 100.0f;

    // Spot light cone angles, in radians (only valid for Spot lights)
    float     SpotInnerAngle       = 0.785398f;
    float     SpotOuterAngle       = 1.047198f;

    // Shadow parameters (meaningful only when Flags has CastsShadows set)
    uint32_t  ShadowResolution     = 0;      // 0 = engine default
    float     ShadowConstantBias   = 1e-4f;
    float     ShadowSlopeScaleBias = 2.0f;
    float     ShadowNormalOffset   = 0.5f;   // in shadow-map texels
};

//------------------------------------------------------------------------------------------------
// On-disk layout checks

namespace CpkgDetail
{
    // True when T can be stored as its sizeof(T) bytes: trivially copyable, standard layout,
    // no more aligned than a payload buffer, and sizeof(T) equal to the sum of the sizes of the
    // members listed, so the compiler inserted no implicit padding. Every member must be listed.
    template <class T, class... TMembers>
    consteval bool IsOnDiskStruct(TMembers T::*...)
    {
        return std::is_trivially_copyable_v<T>
            && std::is_standard_layout_v<T>
            && alignof(T) <= kCpkgPayloadAlignment
            && sizeof(T) == (sizeof(TMembers) + ...);
    }
}

#define CPKG_ASSERT_ON_DISK(T, ...) \
    static_assert(CpkgDetail::IsOnDiskStruct<T>(__VA_ARGS__), #T " is not a valid on-disk struct")

CPKG_ASSERT_ON_DISK(CpkgSpan<char>, &CpkgSpan<char>::Offset, &CpkgSpan<char>::Count);
CPKG_ASSERT_ON_DISK(CpkgCompressionScheme,
    &CpkgCompressionScheme::Codec, &CpkgCompressionScheme::Padding,
    &CpkgCompressionScheme::StoredSize, &CpkgCompressionScheme::RawSize);
CPKG_ASSERT_ON_DISK(CpkgFileHeader,
    &CpkgFileHeader::Magic, &CpkgFileHeader::Version, &CpkgFileHeader::BundleTableOffset,
    &CpkgFileHeader::BundleNameBlobOffset, &CpkgFileHeader::BundleNameBlobSize,
    &CpkgFileHeader::PackageNameOffset, &CpkgFileHeader::BundleCount,
    &CpkgFileHeader::PackageNameLength);
CPKG_ASSERT_ON_DISK(CpkgBundleTableEntry,
    &CpkgBundleTableEntry::AssetTableOffset, &CpkgBundleTableEntry::NameOffset,
    &CpkgBundleTableEntry::AssetNameBlobOffset, &CpkgBundleTableEntry::AssetNameBlobSize,
    &CpkgBundleTableEntry::AssetCount, &CpkgBundleTableEntry::NameLength);
CPKG_ASSERT_ON_DISK(CpkgAssetTableEntry,
    &CpkgAssetTableEntry::PayloadOffset, &CpkgAssetTableEntry::NameOffset,
    &CpkgAssetTableEntry::Type, &CpkgAssetTableEntry::NameLength,
    &CpkgAssetTableEntry::Compression);
CPKG_ASSERT_ON_DISK(CpkgTransform,
    &CpkgTransform::Scale, &CpkgTransform::Rotation, &CpkgTransform::Translation);
CPKG_ASSERT_ON_DISK(CpkgAABB, &CpkgAABB::Min, &CpkgAABB::Max);
CPKG_ASSERT_ON_DISK(CpkgPlacementData,
    &CpkgPlacementData::NodeIndex, &CpkgPlacementData::Mobility,
    &CpkgPlacementData::Bundle, &CpkgPlacementData::Transform);
CPKG_ASSERT_ON_DISK(CpkgWorldRegionRef,
    &CpkgWorldRegionRef::Region, &CpkgWorldRegionRef::StreamingBounds);
CPKG_ASSERT_ON_DISK(CpkgWorldSettingsData,
    &CpkgWorldSettingsData::Regions, &CpkgWorldSettingsData::Skyboxes,
    &CpkgWorldSettingsData::DefaultSkybox, &CpkgWorldSettingsData::ActiveCameraIndex,
    &CpkgWorldSettingsData::BackgroundColor);
CPKG_ASSERT_ON_DISK(CpkgTransformNodeData,
    &CpkgTransformNodeData::ParentIndex, &CpkgTransformNodeData::Mobility,
    &CpkgTransformNodeData::ParentBone, &CpkgTransformNodeData::LocalTransform);
CPKG_ASSERT_ON_DISK(CpkgImportNodeData,
    &CpkgImportNodeData::PlacementIndex, &CpkgImportNodeData::Padding,
    &CpkgImportNodeData::ExportName, &CpkgImportNodeData::LocalTransform);
CPKG_ASSERT_ON_DISK(CpkgExportListData, &CpkgExportListData::NodeIndices);
CPKG_ASSERT_ON_DISK(CpkgSkeletonBone,
    &CpkgSkeletonBone::ParentIndex, &CpkgSkeletonBone::Padding,
    &CpkgSkeletonBone::Name, &CpkgSkeletonBone::RestTransform);
CPKG_ASSERT_ON_DISK(CpkgSkeletonData, &CpkgSkeletonData::Bones);
CPKG_ASSERT_ON_DISK(CpkgSkeletonInstanceData,
    &CpkgSkeletonInstanceData::NodeIndex, &CpkgSkeletonInstanceData::Padding,
    &CpkgSkeletonInstanceData::Skeleton);
CPKG_ASSERT_ON_DISK(CpkgMeshInstanceData,
    &CpkgMeshInstanceData::NodeIndex, &CpkgMeshInstanceData::SkeletonInstanceIndex,
    &CpkgMeshInstanceData::MeshData, &CpkgMeshInstanceData::Skin);
CPKG_ASSERT_ON_DISK(CpkgMeshFieldData,
    &CpkgMeshFieldData::NodeIndex, &CpkgMeshFieldData::CullDistance,
    &CpkgMeshFieldData::MeshData, &CpkgMeshFieldData::Transforms);
CPKG_ASSERT_ON_DISK(CpkgVertexPosition, &CpkgVertexPosition::XYZ);
CPKG_ASSERT_ON_DISK(CpkgVertexNormal, &CpkgVertexNormal::XYZ);
CPKG_ASSERT_ON_DISK(CpkgVertexUV, &CpkgVertexUV::UV);
CPKG_ASSERT_ON_DISK(CpkgVertexTangent, &CpkgVertexTangent::XYZW);
CPKG_ASSERT_ON_DISK(CpkgVertexBoneIndices, &CpkgVertexBoneIndices::Index);
CPKG_ASSERT_ON_DISK(CpkgVertexBoneWeights, &CpkgVertexBoneWeights::Weight);
CPKG_ASSERT_ON_DISK(CpkgMeshDataGroup,
    &CpkgMeshDataGroup::Material, &CpkgMeshDataGroup::Positions, &CpkgMeshDataGroup::Normals,
    &CpkgMeshDataGroup::UV0, &CpkgMeshDataGroup::Tangents, &CpkgMeshDataGroup::BoneIndices,
    &CpkgMeshDataGroup::BoneWeights);
CPKG_ASSERT_ON_DISK(CpkgMeshData,
    &CpkgMeshData::Topology, &CpkgMeshData::LocalBounds, &CpkgMeshData::Padding,
    &CpkgMeshData::Groups);
CPKG_ASSERT_ON_DISK(CpkgDisplacementData,
    &CpkgDisplacementData::DisplacementMap, &CpkgDisplacementData::MapScale,
    &CpkgDisplacementData::MapBias, &CpkgDisplacementData::MinTessFactor,
    &CpkgDisplacementData::MaxTessFactor, &CpkgDisplacementData::DistanceLodScale,
    &CpkgDisplacementData::CurvatureLodScale);
CPKG_ASSERT_ON_DISK(CpkgMaterialData,
    &CpkgMaterialData::BaseColorFactor, &CpkgMaterialData::EmissiveFactor,
    &CpkgMaterialData::RoughMetalAOFactor, &CpkgMaterialData::AlbedoTexture,
    &CpkgMaterialData::NormalTexture, &CpkgMaterialData::RoughnessTexture,
    &CpkgMaterialData::MetallicTexture, &CpkgMaterialData::AmbientOcclusionTexture,
    &CpkgMaterialData::EmissiveTexture, &CpkgMaterialData::HasDisplacement,
    &CpkgMaterialData::Padding, &CpkgMaterialData::Displacement);
CPKG_ASSERT_ON_DISK(CpkgImageData,
    &CpkgImageData::Encoding, &CpkgImageData::Dimension, &CpkgImageData::Width,
    &CpkgImageData::Height, &CpkgImageData::Depth, &CpkgImageData::ArraySize,
    &CpkgImageData::MipLevels, &CpkgImageData::Padding, &CpkgImageData::Images);
CPKG_ASSERT_ON_DISK(CpkgSkyboxData,
    &CpkgSkyboxData::Image, &CpkgSkyboxData::Orientation, &CpkgSkyboxData::Intensity,
    &CpkgSkyboxData::Padding);
CPKG_ASSERT_ON_DISK(CpkgNodeTrack,
    &CpkgNodeTrack::NodeIndex, &CpkgNodeTrack::Property, &CpkgNodeTrack::Times,
    &CpkgNodeTrack::Values, &CpkgNodeTrack::Interpolations, &CpkgNodeTrack::Tangents);
CPKG_ASSERT_ON_DISK(CpkgSkeletalAnimationBinding,
    &CpkgSkeletalAnimationBinding::SkeletonInstanceIndex, &CpkgSkeletalAnimationBinding::Padding,
    &CpkgSkeletalAnimationBinding::Animation);
CPKG_ASSERT_ON_DISK(CpkgAnimationClipData,
    &CpkgAnimationClipData::Duration, &CpkgAnimationClipData::Padding,
    &CpkgAnimationClipData::Tracks, &CpkgAnimationClipData::SkeletalAnimations);
CPKG_ASSERT_ON_DISK(CpkgBoneTrack,
    &CpkgBoneTrack::Bone, &CpkgBoneTrack::Property, &CpkgBoneTrack::Padding,
    &CpkgBoneTrack::Times, &CpkgBoneTrack::Values, &CpkgBoneTrack::Interpolations,
    &CpkgBoneTrack::Tangents);
CPKG_ASSERT_ON_DISK(CpkgSkeletalAnimationData,
    &CpkgSkeletalAnimationData::Skeleton, &CpkgSkeletalAnimationData::Tracks);
CPKG_ASSERT_ON_DISK(CpkgInvBindPose, &CpkgInvBindPose::M);
CPKG_ASSERT_ON_DISK(CpkgMeshSkinData,
    &CpkgMeshSkinData::Skeleton, &CpkgMeshSkinData::Bones, &CpkgMeshSkinData::InvBindPoses);
CPKG_ASSERT_ON_DISK(CpkgCameraData,
    &CpkgCameraData::NodeIndex, &CpkgCameraData::NearClip, &CpkgCameraData::FarClip,
    &CpkgCameraData::FovAngle, &CpkgCameraData::ExposureStops);
CPKG_ASSERT_ON_DISK(CpkgLightData,
    &CpkgLightData::NodeIndex, &CpkgLightData::Type, &CpkgLightData::Color,
    &CpkgLightData::Intensity, &CpkgLightData::Flags, &CpkgLightData::AttenuationConstant,
    &CpkgLightData::AttenuationLinear, &CpkgLightData::AttenuationQuadratic,
    &CpkgLightData::Range, &CpkgLightData::SpotInnerAngle, &CpkgLightData::SpotOuterAngle,
    &CpkgLightData::ShadowResolution, &CpkgLightData::ShadowConstantBias,
    &CpkgLightData::ShadowSlopeScaleBias, &CpkgLightData::ShadowNormalOffset);

#undef CPKG_ASSERT_ON_DISK

//------------------------------------------------------------------------------------------------
// Payload encoding helpers

// Builds one payload in memory. The header slot at offset 0 is reserved on the first call;
// arrays are appended to the data area in call order. The payload buffer is aligned to
// kCpkgPayloadAlignment, so GetPayload can also be decoded in place.
template <class THeader>
    requires std::is_trivially_copyable_v<THeader>
class CCpkgPayloadBuilder
{
public:
    // Pads the data area to alignof(T), appends a copy of elements, and writes the span
    // referring to them to *pSpan. An empty elements appends nothing and writes an empty span.
    // T is deduced from pSpan only, so any contiguous container of T converts to elements.
    template <class T>
        requires std::is_trivially_copyable_v<T>
    Gem::Result Append(std::span<const std::type_identity_t<T>> elements, CpkgSpan<T> *pSpan)
    {
        static_assert(alignof(T) <= kCpkgPayloadAlignment);

        if (!pSpan)
            return Gem::Result::InvalidArg;

        Gem::Result result = ReserveHeader();
        if (Gem::Failed(result))
            return result;

        if (elements.empty())
        {
            *pSpan = {};
            return Gem::Result::Success;
        }

        const size_t offset = (m_Size + alignof(T) - 1) & ~(alignof(T) - 1);
        if (elements.size() > (std::numeric_limits<size_t>::max() - offset) / sizeof(T))
            return Gem::Result::OutOfMemory;
        const size_t byteCount = elements.size() * sizeof(T);

        result = Resize(offset + byteCount);
        if (Gem::Failed(result))
            return result;

        std::memcpy(Data() + offset, elements.data(), byteCount);
        pSpan->Offset = offset;
        pSpan->Count = elements.size();
        return Gem::Result::Success;
    }

    // Copies header into the slot at offset 0. Typically called last, once every span the
    // header holds is known.
    Gem::Result SetHeader(const THeader &header)
    {
        Gem::Result result = ReserveHeader();
        if (Gem::Failed(result))
            return result;

        std::memcpy(Data(), &header, sizeof(THeader));
        return Gem::Result::Success;
    }

    // Pass to CCpkgWriter::AppendAsset as data. Empty until the first call reserves the header.
    std::span<const std::byte> GetPayload() const
    {
        return { reinterpret_cast<const std::byte *>(m_Blocks.data()), m_Size };
    }

private:
    struct alignas(kCpkgPayloadAlignment) Block
    {
        std::byte Bytes[kCpkgPayloadAlignment];
    };

    std::byte *Data()
    {
        return reinterpret_cast<std::byte *>(m_Blocks.data());
    }

    // Reserves the zeroed header slot at offset 0 if no call has yet.
    Gem::Result ReserveHeader()
    {
        return m_Size == 0 ? Resize(sizeof(THeader)) : Gem::Result::Success;
    }

    // Grows the payload to size bytes; new bytes, including alignment gaps, are zero.
    Gem::Result Resize(size_t size)
    {
        try
        {
            m_Blocks.resize((size + kCpkgPayloadAlignment - 1) / kCpkgPayloadAlignment, Block{});
        }
        catch (const std::bad_alloc &)
        {
            return Gem::Result::OutOfMemory;
        }
        catch (const std::length_error &)
        {
            return Gem::Result::OutOfMemory;
        }
        m_Size = size;
        return Gem::Result::Success;
    }

    std::vector<Block> m_Blocks;
    size_t m_Size = 0;
};

namespace CpkgDetail
{
    inline bool IsPayloadBufferValid(std::span<const std::byte> payload)
    {
        return !payload.empty()
            && reinterpret_cast<uintptr_t>(payload.data()) % kCpkgPayloadAlignment == 0;
    }
}

// Returns the payload's header in place. Fails with InvalidArg if payload is empty or not
// aligned to kCpkgPayloadAlignment, and CorruptedData if payload.size() < sizeof(THeader).
template <class THeader>
    requires std::is_trivially_copyable_v<THeader>
Gem::Result CpkgGetPayloadHeader(std::span<const std::byte> payload, const THeader **ppHeader)
{
    if (!ppHeader || !CpkgDetail::IsPayloadBufferValid(payload))
        return Gem::Result::InvalidArg;
    if (payload.size() < sizeof(THeader))
        return Gem::Result::CorruptedData;

    *ppHeader = reinterpret_cast<const THeader *>(payload.data());
    return Gem::Result::Success;
}

// Checks span against the Payload Layout rules and returns its elements in place, or an empty
// std::span for an empty span. Fails with InvalidArg if payload is empty or misaligned, and
// CorruptedData if the span is misaligned, overlaps the header, or extends past the end of
// payload.
template <class THeader, class T>
    requires std::is_trivially_copyable_v<T>
Gem::Result CpkgResolveSpan(std::span<const std::byte> payload, const CpkgSpan<T> &span, std::span<const T> *pElements)
{
    if (!pElements || !CpkgDetail::IsPayloadBufferValid(payload))
        return Gem::Result::InvalidArg;

    if (span.Count == 0)
    {
        if (span.Offset != 0)
            return Gem::Result::CorruptedData;
        *pElements = {};
        return Gem::Result::Success;
    }

    const uint64_t size = payload.size();
    if (span.Offset % alignof(T) != 0
        || span.Offset < sizeof(THeader)
        || span.Offset > size
        || span.Count > (size - span.Offset) / sizeof(T))
    {
        return Gem::Result::CorruptedData;
    }

    *pElements = { reinterpret_cast<const T *>(payload.data() + span.Offset), static_cast<size_t>(span.Count) };
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
// Package files

class CCpkgFile;

namespace CpkgDetail
{
    // Hashes names for maps and sets that look up std::string keys by std::string_view.
    struct NameHash
    {
        using is_transparent = void;
        size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
    };

    using NameSet = std::unordered_set<std::string, NameHash, std::equal_to<>>;
}

// Writes a package file. Bundle and asset writes are append-only, with a single fixup of the
// file header in Close. A single-threaded, call-in-order API.
class CCpkgWriter
{
public:
    CCpkgWriter();
    ~CCpkgWriter();
    CCpkgWriter(const CCpkgWriter &) = delete;
    CCpkgWriter &operator=(const CCpkgWriter &) = delete;

    // Creates fileName, which must not exist, and writes the file header and packageName.
    Gem::Result Open(const char *fileName, std::string_view packageName);

    // Writes the bundle table and bundle name blob, fixes up the file header, and closes the file.
    Gem::Result Close();

    // Begins a new bundle; every BeginBundle is matched by an EndBundle.
    Gem::Result BeginBundle(std::string_view bundleName, uint32_t *pBundleIndex = nullptr);

    // Seals the open bundle: appends its asset table and asset name blob.
    Gem::Result EndBundle();

    // Appends one asset to the open bundle. data is the raw payload, stored with codec.
    Gem::Result AppendAsset(AssetType assetType, std::string_view name, CpkgCodec codec, std::span<const std::byte> data, uint32_t *pAssetIndex = nullptr);

private:
    bool IsOpen() const { return m_pFile != nullptr; }
    Gem::Result Write(const void *pData, size_t size);
    Gem::Result Fail(Gem::Result result);
    void Reset();

    std::unique_ptr<CCpkgFile> m_pFile;
    std::string m_FileName;
    uint32_t m_PackageNameLength = 0;
    uint64_t m_EndOffset = 0;

    std::vector<CpkgBundleTableEntry> m_BundleTable;
    std::string m_BundleNameBlob;
    CpkgDetail::NameSet m_BundleNames;

    bool m_BundleOpen = false;
    std::vector<CpkgAssetTableEntry> m_AssetTable;
    std::string m_AssetNameBlob;
    CpkgDetail::NameSet m_AssetNames;
};

// Built by BuildBundleDirectory/BuildAssetDirectory. Owns its own copies of the names it
// indexes, so it stays valid even after the CCpkgReader that built it is closed.
class CCpkgNameDirectory
{
public:
    // Returns the index associated with name (a bundle index if this directory came from
    // BuildBundleDirectory, an asset index if it came from BuildAssetDirectory), or UINT32_MAX
    // if name is not present.
    uint32_t Find(std::string_view name) const
    {
        auto it = m_Entries.find(name);
        return it == m_Entries.end() ? UINT32_MAX : it->second;
    }

private:
    std::unordered_map<std::string, uint32_t, CpkgDetail::NameHash, std::equal_to<>> m_Entries;
    friend class CCpkgReader;
};

// Reads a package file. The reader's state does not change between Open and Close, so every
// const method is safe to call concurrently on one instance.
class CCpkgReader
{
public:
    CCpkgReader();
    ~CCpkgReader();
    CCpkgReader(const CCpkgReader &) = delete;
    CCpkgReader &operator=(const CCpkgReader &) = delete;

    // Opens a package and loads and validates its structure: the package name, the bundle
    // table and bundle name blob, and every bundle's asset table and asset name blob.
    Gem::Result Open(const char *fileName);
    Gem::Result Close();

    std::string_view GetPackageName() const;
    uint32_t GetBundleCount() const;
    std::string_view GetBundleName(uint32_t bundleIndex) const;
    uint32_t GetAssetCount(uint32_t bundleIndex) const;
    AssetType GetAssetType(uint32_t bundleIndex, uint32_t assetIndex) const;
    CpkgCompressionScheme GetAssetCompression(uint32_t bundleIndex, uint32_t assetIndex) const;
    std::string_view GetAssetName(uint32_t bundleIndex, uint32_t assetIndex) const;

    // Reads one asset's raw payload into data, which holds at least its RawSize bytes.
    Gem::Result ReadAsset(uint32_t bundleIndex, uint32_t assetIndex, std::span<std::byte> data) const;

    Gem::Result BuildBundleDirectory(CCpkgNameDirectory *pDirectory) const;
    Gem::Result BuildAssetDirectory(uint32_t bundleIndex, CCpkgNameDirectory *pDirectory, AssetType typeFilter = AssetType::Unknown) const;

private:
    struct Bundle
    {
        CpkgBundleTableEntry Entry;
        std::vector<CpkgAssetTableEntry> Assets;
        std::string AssetNameBlob;
    };

    bool IsOpen() const { return m_pFile != nullptr; }
    const CpkgAssetTableEntry *FindAsset(uint32_t bundleIndex, uint32_t assetIndex) const;
    Gem::Result Load(uint64_t fileSize);
    void Reset();

    std::unique_ptr<CCpkgFile> m_pFile;
    std::string m_PackageName;
    std::string m_BundleNameBlob;
    std::vector<Bundle> m_Bundles;
};

} // namespace Canvas
