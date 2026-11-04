//================================================================================================
// CanvasFbx - Scene importer interface
//
// Defines the data contracts returned by the importer so that consumers (e.g.
// CanvasModelViewer) can create Canvas scene-graph objects without knowing
// anything about FBX SDK internals.
//
// The importer converts all data to Canvas conventions on output:
//   - Row-vector matrices
//   - Right-handed, Z-up world, in meters
//   - Canvas local basis: +X = forward, +Y = left, +Z = up. A model's front
//     faces +X, and cameras and lights look along their local +X
//   - Positions / normals as FloatVector4 (W = 1 for positions, 0 for normals)
//================================================================================================
#pragma once

#include "CanvasCore.h"
#include "CanvasMath.hpp"
#include <Gem.hpp>

#include <vector>
#include <string>
#include <cstdint>
#include <functional>

// CanvasFbx is a static library; no dllexport/dllimport plumbing needed.
#define CANVASFBX_API

namespace Canvas
{
namespace Fbx
{

//------------------------------------------------------------------------------------------------
// Diagnostic severity
//------------------------------------------------------------------------------------------------
enum class DiagLevel : uint8_t
{
    Info,
    Warning,
    Error,
};

//------------------------------------------------------------------------------------------------
// Single diagnostic message produced during import
//------------------------------------------------------------------------------------------------
struct ImportDiag
{
    DiagLevel       Level;
    std::string     Message;
};

//------------------------------------------------------------------------------------------------
// Reference to a texture image. The importer always populates AbsoluteFilePath
// (used as the cache key downstream). When the FBX file embeds the texture
// payload, EmbeddedBytes also carries the raw image bytes (eg. PNG/JPG); the
// caller can decode them in-memory rather than re-reading from disk.
//------------------------------------------------------------------------------------------------
struct ImportedTextureRef
{
    std::string             AbsoluteFilePath;   // resolved filesystem path
    bool                    Embedded = false;   // true when payload is in EmbeddedBytes
    std::vector<uint8_t>    EmbeddedBytes;      // raw image bytes (only when Embedded)
};

//------------------------------------------------------------------------------------------------
// Imported material. Texture indices reference ImportedScene::Textures; -1 means
// no texture is bound for that role and the *Factor field carries a constant
// fallback value (which is also a tint when a texture *is* bound).
//------------------------------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable: 4324) // structure padded due to alignment specifier (FloatVector4 is alignas(16))
struct ImportedMaterial
{
    std::string             Name;
    Math::FloatVector4      BaseColorFactor    = { 1.0f, 1.0f, 1.0f, 1.0f }; // linear RGBA
    Math::FloatVector4      EmissiveFactor     = { 0.0f, 0.0f, 0.0f, 0.0f }; // linear RGB, A unused
    // R=Roughness, G=Metallic, B=AmbientOcclusion, A=spare. Defaults match a
    // fully-rough, non-metallic, fully-lit surface.
    Math::FloatVector4      RoughMetalAOFactor = { 1.0f, 0.0f, 1.0f, 0.0f };

    int32_t                 AlbedoTextureIndex           = -1; // -> ImportedScene::Textures
    int32_t                 NormalTextureIndex           = -1;
    int32_t                 EmissiveTextureIndex         = -1;
    int32_t                 RoughnessTextureIndex        = -1;
    int32_t                 MetallicTextureIndex         = -1;
    int32_t                 AmbientOcclusionTextureIndex = -1;
};
#pragma warning(pop)

//------------------------------------------------------------------------------------------------
// Per-vertex skin influences (up to 4 bones).
// Weights are normalised to sum to 1.  BoneIndices index into ImportedSkin::BoneNodeIndices.
//------------------------------------------------------------------------------------------------
struct ImportedSkinVertex
{
    uint32_t BoneIndices[4] = { 0, 0, 0, 0 };
    float    Weights[4]     = { 1.0f, 0.0f, 0.0f, 0.0f };
};

//------------------------------------------------------------------------------------------------
// One material partition of an imported mesh. Each part holds its own
// expanded, non-indexed vertex streams (triangle-list topology) so it can be
// uploaded as a self-contained vertex range. UV0 and Tangents are empty when
// the source mesh did not provide them.
//
// Tangents are stored as float4: xyz = tangent vector, w = bitangent sign
// (+1 / -1) so the pixel shader can reconstruct B = sign * cross(N, T).
//------------------------------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable: 4324) // structure padded due to alignment specifier (FloatVector4 is alignas(16))
struct ImportedMeshPart
{
    int32_t                             MaterialIndex = -1; // -> ImportedScene::Materials, -1 when none
    std::vector<Math::FloatVector4>     Positions;          // W = 1
    std::vector<Math::FloatVector4>     Normals;            // W = 0, unit length
    std::vector<Math::FloatVector2>     UV0;                // empty when missing
    std::vector<Math::FloatVector4>     Tangents;           // xyz = T, w = bitangent sign; empty when missing
    std::vector<ImportedSkinVertex>     SkinVertices;       // one per vertex; empty when no skin deformer

    uint32_t GetVertexCount() const { return static_cast<uint32_t>(Positions.size()); }
};
#pragma warning(pop)

//------------------------------------------------------------------------------------------------
// Imported mesh geometry. A mesh always has at least one ImportedMeshPart.
// Multi-material source meshes produce one part per material partition; the
// runtime is expected to render them as N XMeshInstance instances sharing one
// XGfxMeshData with N material groups.
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
// Skinning binding for one mesh: bone list + inverse bind-pose matrices.
// BoneNodeIndices[i] -> ImportedScene::Nodes[i] (the scene node that is the bone).
// InvBindPoses[i]   transforms a vertex from mesh/geometry space into bone i's local space
//                   at the time the skin was bound.
// When HasSkin == false the vectors are empty and SkinVertices in each mesh part is empty.
//------------------------------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable: 4324)
struct ImportedSkin
{
    bool                                HasSkin = false;
    std::vector<int32_t>                BoneNodeIndices;    // one per cluster
    std::vector<Math::FloatMatrix4x4>   InvBindPoses;       // one per cluster, Canvas row-vector space
};
#pragma warning(pop)

//------------------------------------------------------------------------------------------------
// Imported mesh geometry. A mesh always has at least one ImportedMeshPart.
// Multi-material source meshes produce one part per material partition; the
// runtime is expected to render them as N XMeshInstance instances sharing one
// XGfxMeshData with N material groups.
//------------------------------------------------------------------------------------------------
struct ImportedMesh
{
    std::string                         Name;
    std::vector<ImportedMeshPart>       Parts;      // always >= 1 entry on success
    Math::AABB                          Bounds;     // AABB across all parts (Canvas space)
    ImportedSkin                        Skin;       // empty (HasSkin==false) when no skin deformer
};

//------------------------------------------------------------------------------------------------
// Imported light (mapped to Canvas::LightType)
//------------------------------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable: 4324) // structure padded due to alignment specifier (FloatVector4 is alignas(16))
struct ImportedLight
{
    std::string         Name;
    Canvas::LightType   Type            = Canvas::LightType::Directional;
    Math::FloatVector4  Color           = { 1.0f, 1.0f, 1.0f, 1.0f }; // linear RGBA
    float               Intensity       = 1.0f;

    // Point / Spot
    float               Range           = 0.0f;
    float               AttenuationConst  = 1.0f;
    float               AttenuationLinear = 0.0f;
    float               AttenuationQuad   = 0.0f;

    // Spot only
    float               SpotInnerAngle  = 0.0f;    // radians
    float               SpotOuterAngle  = 0.0f;    // radians
};
#pragma warning(pop)

//------------------------------------------------------------------------------------------------
// Imported camera
//------------------------------------------------------------------------------------------------
struct ImportedCamera
{
  std::string         Name;
  float               NearClip        = 0.1f;
  float               FarClip         = 1000.0f;
  float               FovAngle        = static_cast<float>(Math::Pi / 4.0); // radians, vertical when available
  float               AspectRatio     = 16.0f / 9.0f;
};

//------------------------------------------------------------------------------------------------
// A node in the imported scene hierarchy.
// Indices refer into ImportedScene::Nodes (self-referencing flat array).
// MeshIndex / LightIndex are -1 when no payload is bound.
//------------------------------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable: 4324) // structure padded due to alignment specifier (FloatVector4 is alignas(16))
struct ImportedNode
{
    std::string             Name;
    int32_t                 ParentIndex     = -1;   // -1 = root

    // Local transform (Canvas convention: row-vector, Z-up), in the bind pose: the file's
    // recorded bind matrices where it has them, the stored local transform otherwise.
    Transform               LocalTransform;

    // Payload binding (-1 = none)
    int32_t                 MeshIndex       = -1;       // -> ImportedScene::Meshes[i]
    int32_t                 LightIndex      = -1;       // -> ImportedScene::Lights[i]
    int32_t                 CameraIndex     = -1;       // -> ImportedScene::Cameras[i]
};
#pragma warning(pop)

//------------------------------------------------------------------------------------------------
// The property of a node's local transform that an imported track animates, and the form of
// its values.
//------------------------------------------------------------------------------------------------
enum class ImportedTransformProperty : uint32_t
{
    Translation = 0,    // a point, W = 1
    Rotation    = 1,    // a unit quaternion, as (X, Y, Z, W)
    Scale       = 2,    // a vector, W = 0
};

//------------------------------------------------------------------------------------------------
// The keys of one property (translation, rotation, or scale) of one node within a clip, in the
// forms ImportedTransformProperty describes.  Rotation keys are quaternions (ufbx converts Euler
// rotation to quaternions), each in the same hemisphere as the key before it.
//------------------------------------------------------------------------------------------------
struct ImportedAnimationTrack
{
    int32_t                             NodeIndex = -1;     // -> ImportedScene::Nodes
    ImportedTransformProperty           Property = ImportedTransformProperty::Translation;
    AnimationInterpolation              Interpolation = AnimationInterpolation::Linear;
    std::vector<float>                  Times;              // seconds from clip start, ascending
    std::vector<Math::FloatVector4>     Values;             // parallel to Times
};

//------------------------------------------------------------------------------------------------
// One FBX AnimationStack (== one Blender Action).
// A file may contain zero or more of these.
//
// A property the clip holds at its node's imported pose (ImportedNode::LocalTransform) has no
// track; one it holds anywhere else has a single-key track.
//------------------------------------------------------------------------------------------------
struct ImportedAnimationClip
{
    std::string                         Name;               // from stack->name (Blender Action name)
    float                               DurationSeconds;
    std::vector<ImportedAnimationTrack> Tracks;             // at most one per node and property
};

//------------------------------------------------------------------------------------------------
// Complete result of an import operation
//------------------------------------------------------------------------------------------------
struct ImportedScene
{
    std::vector<ImportedMesh>           Meshes;
    std::vector<ImportedLight>          Lights;
    std::vector<ImportedCamera>         Cameras;
    std::vector<ImportedMaterial>       Materials;
    std::vector<ImportedTextureRef>     Textures;
    std::vector<ImportedNode>           Nodes;
    std::vector<ImportedAnimationClip>  AnimationClips;     // one per action; stacks named "Object|Action" merge by Action
    std::vector<ImportDiag>             Diagnostics;
    Math::AABB                          SceneBounds;    // world-space bounds of all mesh instances at rest
    int32_t                             ActiveCameraNodeIndex = -1;

    bool HasMeshes()     const { return !Meshes.empty(); }
    bool HasLights()     const { return !Lights.empty(); }
    bool HasCameras()    const { return !Cameras.empty(); }
    bool HasMaterials()  const { return !Materials.empty(); }
    bool HasAnimations() const { return !AnimationClips.empty(); }
    bool HasErrors()     const;
};

//------------------------------------------------------------------------------------------------
// Import options (extensible)
//------------------------------------------------------------------------------------------------
struct ImportOptions
{
    bool    Triangulate         = true;     // force triangulation of all polygons
    bool    GenerateNormals     = true;     // generate normals if missing
};

//================================================================================================
// Importer entry point
//
// Returns Gem::Result::Success on success (scene may still carry warnings in Diagnostics).
// Returns a failure result on hard failure; Diagnostics explains why.
//================================================================================================
CANVASFBX_API Gem::Result ImportScene(
    _In_z_  const wchar_t  *pFilePath,
    _In_    const ImportOptions &options,
    _Out_   ImportedScene  *pScene
);

//================================================================================================
// BuildModel - convert an ImportedScene into a fully populated XModel
//
// Builds an XModel, through an XModelBuilder, containing GPU mesh data,
// materials, textures, lights, cameras, and the imported hierarchy.  The caller
// supplies a texture-loading callback because image decoding is
// platform-specific (e.g. WIC on Windows, stb_image elsewhere).
//
// The bones of skins that share a bone, and every node between those bones and
// their nearest common ancestor, become the joints of one skeleton, in the space
// of that ancestor.  Every other imported node becomes a model node.  Content on
// a joint, and nodes parented to one, hang from a socket on that joint.  An
// imported node's tracks animate its joint when the node is a joint, and its
// model node otherwise.
//
// Behaviour on malformed data:
//   - Out-of-range material / mesh / light / camera / parent indices are
//     skipped (with a warning to pLogger when provided).
//   - Empty mesh parts are skipped.
//   - A skin with a bone outside the hierarchy is dropped, and its mesh draws
//     unskinned.  A skinned mesh carried by several nodes is skinned on the
//     first and draws unskinned on the others.
//   - A scene with no meshes is valid; cameras, lights, and nodes are still
//     built into the model.
//
// Returns Gem::Result::Success on success.  The returned XModel is not yet instantiated;
// call XModel::Instantiate() to place it in a scene graph.
//================================================================================================

//------------------------------------------------------------------------------------------------
// Callback for application-specific texture decoding.
//
// Called once per entry in ImportedScene::Textures. Return true and write an
// AddRef'd surface to *ppSurface on success. Return false (and leave
// *ppSurface null) to leave the corresponding material slot unbound.
//------------------------------------------------------------------------------------------------
using TextureLoadFn = std::function<bool(
    Canvas::XGfxDevice  *pDevice,
    const ImportedTextureRef &ref,
    Canvas::XGfxSurface **ppSurface)>;

//------------------------------------------------------------------------------------------------
// Options for BuildModel
//------------------------------------------------------------------------------------------------
struct BuildModelOptions
{
    TextureLoadFn       TextureLoader;              // optional; null -> textures are not loaded
    const char         *pModelName      = nullptr;  // optional; name given to the XModel (nullptr -> "ImportedModel")
    Canvas::XLogger    *pLogger         = nullptr;  // optional; receives warnings for skipped data
};

CANVASFBX_API Gem::Result BuildModel(
    _In_    Canvas::XCanvas        *pCanvas,
    _In_    Canvas::XGfxDevice     *pDevice,
    _In_    const ImportedScene    &scene,
    _In_    const BuildModelOptions &options,
    _Out_   Canvas::XModel        **ppModel
);

} // namespace Fbx
} // namespace Canvas

