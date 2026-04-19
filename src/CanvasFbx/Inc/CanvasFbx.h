//================================================================================================
// CanvasFbx — Scene importer interface
//
// Defines the data contracts returned by the importer so that consumers (e.g.
// CanvasModelViewer) can create Canvas scene-graph objects without knowing
// anything about FBX SDK internals.
//
// The importer converts all data to Canvas conventions on output:
//   - Row-vector matrices (v' = v * M), translation in row[3]
//   - Z-up coordinate system (X = right, Y = forward, Z = up)
//   - Positions / normals as FloatVector4 (W = 1 for positions, 0 for normals)
//================================================================================================
#pragma once

#include "CanvasCore.h"
#include "CanvasMath.hpp"

#include <vector>
#include <string>
#include <cstdint>

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

    uint32_t GetVertexCount() const { return static_cast<uint32_t>(Positions.size()); }
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

    // Local transform (Canvas convention: row-vector, Z-up)
    Math::FloatVector4      Translation     = { 0.0f, 0.0f, 0.0f, 0.0f };
    Math::FloatQuaternion   Rotation        = {}; // identity
    Math::FloatVector4      Scale           = { 1.0f, 1.0f, 1.0f, 0.0f };

    // Payload binding (-1 = none)
    int32_t                 MeshIndex       = -1;       // → ImportedScene::Meshes[i]
    int32_t                 LightIndex      = -1;       // → ImportedScene::Lights[i]
    int32_t                 CameraIndex     = -1;       // → ImportedScene::Cameras[i]
};
#pragma warning(pop)

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
    std::vector<ImportDiag>             Diagnostics;
    Math::AABB                          SceneBounds;    // union of all mesh bounds (Canvas space)
    int32_t                             ActiveCameraNodeIndex = -1;

    bool HasMeshes()    const { return !Meshes.empty(); }
    bool HasLights()    const { return !Lights.empty(); }
    bool HasCameras()   const { return !Cameras.empty(); }
    bool HasMaterials() const { return !Materials.empty(); }
    bool HasErrors()    const;
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
// Returns S_OK on success (scene may still carry warnings in Diagnostics).
// Returns an error HRESULT on hard failure; Diagnostics explains why.
//================================================================================================
CANVASFBX_API HRESULT ImportScene(
    _In_z_  const wchar_t  *pFilePath,
    _In_    const ImportOptions &options,
    _Out_   ImportedScene  *pScene
);

} // namespace Fbx
} // namespace Canvas
