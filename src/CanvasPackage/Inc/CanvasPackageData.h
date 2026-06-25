//================================================================================================
// CanvasPackageData - CanvasPackage's CPU-side representation of a packaged 3D scene and its
// .cpkg serialization.
//
// Pure data (no GPU types) plus the native read/write that loads and stores it as a .cpkg
// package.
//================================================================================================
#pragma once

#include "CanvasMath.hpp"
#include "CanvasTypes.h"
#include "Gem.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Canvas
{

// Severity passed to a package I/O logging callback.
enum class PackageLogLevel : uint8_t { Info, Warning, Error };

// Optional logging hook for package read/write. The caller supplies a sink (typically one
// that forwards to QLog / Canvas::XLogger); CanvasPackage stays decoupled from any concrete
// logger. An empty function (the default) silences logging.
using PackageLogFn = std::function<void(PackageLogLevel level, const char* message)>;

#pragma warning(push)
#pragma warning(disable: 4324) // structure padded due to alignment specifier (FloatVector4 is alignas(16))

//--------------------------------------------------------------------------------------------------
// PackageTexture - one entry in the TXTR chunk.
// When EmbeddedBytes is non-empty, the image bytes are stored directly in the package.
// When empty, Path is a .cpkg-relative path to the image file on disk.
// Name is the runtime lookup key; empty for unnamed FBX-sourced textures.
//--------------------------------------------------------------------------------------------------
struct PackageTexture
{
    std::string           Name;
    std::string           Path;
    Canvas::GfxFormat     Format    = Canvas::GfxFormat::Unknown;
    uint32_t              Width     = 0;
    uint32_t              Height    = 0;
    uint32_t              MipCount  = 0;
    std::vector<uint8_t>  EmbeddedBytes;
};

//--------------------------------------------------------------------------------------------------
// PackageMaterial - PBR metallic-roughness material.
// Texture indices reference PackageData::Textures; -1 means the slot is unbound.
//--------------------------------------------------------------------------------------------------
struct PackageMaterial
{
    std::string         Name;
    Math::FloatVector4  BaseColorFactor    = { 1.0f, 1.0f, 1.0f, 1.0f }; // linear RGBA
    Math::FloatVector4  EmissiveFactor     = { 0.0f, 0.0f, 0.0f, 0.0f }; // linear RGB, A unused
    // R=Roughness, G=Metallic, B=AmbientOcclusion, A=spare
    Math::FloatVector4  RoughMetalAOFactor = { 1.0f, 0.0f, 1.0f, 0.0f };

    int32_t AlbedoTextureIndex           = -1;
    int32_t NormalTextureIndex           = -1;
    int32_t EmissiveTextureIndex         = -1;
    int32_t RoughnessTextureIndex        = -1;
    int32_t MetallicTextureIndex         = -1;
    int32_t AmbientOcclusionTextureIndex = -1;
};

//--------------------------------------------------------------------------------------------------
// PackageSkinVertex - per-vertex bone influences (up to 4 bones).
// BoneWeights are normalised to sum to 1. BoneIndices index into PackageSkin::BoneNodeIndices.
//--------------------------------------------------------------------------------------------------
struct PackageSkinVertex
{
    uint32_t BoneIndices[4] = { 0, 0, 0, 0 };
    float    BoneWeights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
};

//--------------------------------------------------------------------------------------------------
// PackageMeshPart - one material partition of a mesh (triangle-list topology).
// UV0 and Tangents are empty when the source mesh did not provide them.
// SkinVertices is empty when the mesh has no skin deformer.
//--------------------------------------------------------------------------------------------------
struct PackageMeshPart
{
    int32_t                         MaterialIndex = -1;
    std::vector<Math::FloatVector4> Positions;   // W = 1
    std::vector<Math::FloatVector4> Normals;     // W = 0, unit length
    std::vector<Math::FloatVector2> UV0;
    std::vector<Math::FloatVector4> Tangents;    // xyz = T, w = bitangent sign
    std::vector<PackageSkinVertex>  SkinVertices;
};

//--------------------------------------------------------------------------------------------------
// PackageSkin - bone list and inverse bind-pose matrices for one mesh.
// BoneNodeIndices[i] indexes into PackageData::Nodes.
// InvBindPoses[i] transforms a vertex from mesh space into bone i's local space at bind time.
//--------------------------------------------------------------------------------------------------
struct PackageSkin
{
    bool                              HasSkin = false;
    std::vector<int32_t>              BoneNodeIndices;
    std::vector<Math::FloatMatrix4x4> InvBindPoses;
};

//--------------------------------------------------------------------------------------------------
// PackageMesh - all geometry for one named mesh object.
//--------------------------------------------------------------------------------------------------
struct PackageMesh
{
    std::string                  Name;
    std::vector<PackageMeshPart> Parts;
    Math::AABB                   Bounds;
    PackageSkin                  Skin;
};

//--------------------------------------------------------------------------------------------------
// PackageLight - a single light source.
//--------------------------------------------------------------------------------------------------
struct PackageLight
{
    std::string        Name;
    Canvas::LightType  Type              = Canvas::LightType::Directional;
    Math::FloatVector4 Color             = { 1.0f, 1.0f, 1.0f, 1.0f }; // linear RGBA
    float              Intensity         = 1.0f;
    float              Range             = 0.0f;
    float              AttenuationConst  = 1.0f;
    float              AttenuationLinear = 0.0f;
    float              AttenuationQuad   = 0.0f;
    float              SpotInnerAngle    = 0.0f; // radians
    float              SpotOuterAngle    = 0.0f; // radians
};

//--------------------------------------------------------------------------------------------------
// PackageCamera - a camera definition.
//--------------------------------------------------------------------------------------------------
struct PackageCamera
{
    std::string Name;
    float       NearZ       = 0.1f;
    float       FarZ        = 1000.0f;
    float       FovY        = static_cast<float>(Math::Pi / 4.0); // radians, vertical FOV
    float       AspectRatio = 16.0f / 9.0f;
};

//--------------------------------------------------------------------------------------------------
// PackageNode - one node in the flat hierarchy.
// ParentIndex = -1 marks a root node. Payload indices = -1 mean no payload is bound.
//--------------------------------------------------------------------------------------------------
struct PackageNode
{
    std::string           Name;
    int32_t               ParentIndex = -1;
    Math::FloatVector4    Translation = { 0.0f, 0.0f, 0.0f, 0.0f };
    Math::FloatVector4    Scale       = { 1.0f, 1.0f, 1.0f, 0.0f };
    Math::FloatQuaternion Rotation    = {};
    int32_t               MeshIndex   = -1;
    int32_t               LightIndex  = -1;
    int32_t               CameraIndex = -1;
};

//--------------------------------------------------------------------------------------------------
// PackageAnimKeyframe - one TRS sample for a node at a given time.
//--------------------------------------------------------------------------------------------------
struct PackageAnimKeyframe
{
    float                 Time;
    Math::FloatVector4    Translation; // W = 0
    Math::FloatQuaternion Rotation;    // unit quaternion (Canvas space)
    Math::FloatVector4    Scale;       // W = 0
};

//--------------------------------------------------------------------------------------------------
// PackageAnimTrack - all TRS keyframes for one node within a clip.
//--------------------------------------------------------------------------------------------------
struct PackageAnimTrack
{
    int32_t                          NodeIndex = -1;
    std::vector<PackageAnimKeyframe> Keyframes;
};

//--------------------------------------------------------------------------------------------------
// PackageAnimClip - one named animation clip (one FBX AnimationStack / Blender Action).
//--------------------------------------------------------------------------------------------------
struct PackageAnimClip
{
    std::string                   Name;
    float                         DurationSeconds = 0.0f;
    std::vector<PackageAnimTrack> Tracks;
};

//--------------------------------------------------------------------------------------------------
// PackageData - complete CPU-side contents of a .cpkg package. Read from / written to a .cpkg
// file via the members below. Foreign formats (e.g. FBX) are imported by separate converters
// that populate a PackageData; only .cpkg is its native on-disk form.
//--------------------------------------------------------------------------------------------------
struct PackageData
{
    std::vector<PackageMesh>      Meshes;
    std::vector<PackageLight>     Lights;
    std::vector<PackageCamera>    Cameras;
    std::vector<PackageMaterial>  Materials;
    std::vector<PackageTexture>   Textures;
    std::vector<PackageNode>      Nodes;
    std::vector<PackageAnimClip>  AnimClips;
    Math::AABB                    Bounds;
    int32_t                       ActiveCameraNodeIndex = -1;

    // Read a .cpkg file into this PackageData. Per-chunk warnings are reported to logFn when
    // one is supplied.
    Gem::Result ReadPackage(const wchar_t* pFilePath, const PackageLogFn& logFn = {});

    // Write this PackageData to a .cpkg file. Warnings are reported to logFn when supplied.
    Gem::Result WritePackage(const wchar_t* pOutputPath, const PackageLogFn& logFn = {}) const;
};

#pragma warning(pop)

} // namespace Canvas
