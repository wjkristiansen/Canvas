//================================================================================================
// SceneConfig
//
// Parsed representation of a CanvasTerrainViewer scene JSON file. Describes
// "what to render" (terrain tiles, material atlas, biome thresholds, camera
// starting vantage) for one scene.
//
// What this is NOT:
//   - Viewer / user preferences (camera speed, mouse sensitivity, day/night
//     cycle settings, log destinations). Those belong on the CLI / user
//     config because they're orthogonal to the scene's content.
//
// Override model: the JSON file provides defaults; CLI flags stomp matching
// fields after the file is loaded.
//================================================================================================

#pragma once

#include "CanvasMath.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace Canvas
{
namespace TerrainViewer
{

struct SceneTile
{
    std::filesystem::path Heightmap;
    float                 Dxy           = 1.0f;
    float                 HeightScale   = 64.0f;
    float                 HeightBias    = 0.0f;
    float                 OriginX       = 0.0f;
    float                 OriginY       = 0.0f;
};

struct SceneBiome
{
    float SandMaxMeters  = 4.0f;
    float SandFadeMeters = 8.0f;
    float SnowMinMeters  = 42.0f;
    float SnowFadeMeters = 52.0f;
    float SlopeRockMin   = 0.10f;
    float SlopeRockMax   = 0.30f;
};

struct SceneMaterial
{
    std::filesystem::path AtlasAlbedo;
    std::filesystem::path AtlasORM;
    float                 AtlasRepeatMeters = 8.0f;
    SceneBiome            Biome;
};

struct SceneCamera
{
    // World-space start pose. LookAt resolves the direction; Position is
    // resolved to a node translation.
    Math::FloatVector4 StartPosition{ 0.0f, -800.0f, 100.0f, 0.0f };
    Math::FloatVector4 StartLookAt  { 0.0f,    0.0f,   0.0f, 0.0f };
    float              FovDegrees = 60.0f;
};

struct SceneConfig
{
    std::string             Name = "default";
    std::vector<SceneTile>  Tiles;
    SceneMaterial           Material;
    SceneCamera             Camera;
};

// Populate a SceneConfig with defaults appropriate for the sample
// CanvasTerrainViewer launch (single fbm tile, sample atlas, centered camera).
void ApplyDefaults(SceneConfig& cfg);

// Load a JSON scene file from disk. Paths inside the file are resolved
// relative to the file's own directory. On parse / IO failure, logs an error
// and returns false.
bool LoadSceneConfig(
    const std::filesystem::path& path,
    SceneConfig*                 outConfig,
    XLogger*                     pLogger = nullptr);

} // namespace TerrainViewer
} // namespace Canvas
