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

#if defined(_WIN32)
  #if defined(CANVASFBX_EXPORTS)
    #define CANVASFBX_API __declspec(dllexport)
  #else
    #define CANVASFBX_API __declspec(dllimport)
  #endif
#else
  #define CANVASFBX_API
#endif

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
// Imported mesh geometry (expanded, non-indexed vertex streams)
//
// Positions and Normals arrays have identical length (one entry per vertex).
// Triangle list topology — every 3 consecutive vertices form a triangle.
//------------------------------------------------------------------------------------------------
struct ImportedMesh
{
    std::string                         Name;
    std::vector<Math::FloatVector4>     Positions;  // W = 1
    std::vector<Math::FloatVector4>     Normals;    // W = 0, unit length
    Math::AABB                          Bounds;     // AABB of positions (Canvas space)

    uint32_t GetVertexCount() const { return static_cast<uint32_t>(Positions.size()); }
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
};
#pragma warning(pop)

//------------------------------------------------------------------------------------------------
// Complete result of an import operation
//------------------------------------------------------------------------------------------------
struct ImportedScene
{
    std::vector<ImportedMesh>   Meshes;
    std::vector<ImportedLight>  Lights;
    std::vector<ImportedNode>   Nodes;
    std::vector<ImportDiag>     Diagnostics;
    Math::AABB                  SceneBounds;    // union of all mesh bounds (Canvas space)

    bool HasMeshes()  const { return !Meshes.empty(); }
    bool HasLights()  const { return !Lights.empty(); }
    bool HasErrors()  const;
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
