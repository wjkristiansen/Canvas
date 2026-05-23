#include "pch.h"
#include "SceneConfig.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace Canvas
{
namespace TerrainViewer
{

namespace
{

using nlohmann::json;

// Resolve a path string from the JSON against a base directory. Absolute
// paths pass through; relative paths anchor at the scene file's directory.
std::filesystem::path ResolvePath(const std::string& s, const std::filesystem::path& baseDir)
{
    if (s.empty())
        return {};
    std::filesystem::path p = std::filesystem::u8path(s);
    if (p.is_absolute())
        return p;
    return std::filesystem::weakly_canonical(baseDir / p);
}

// Read an [x, y] / [x, y, z] / [x, y, z, w] array into a FloatVector4.
// Missing components default to wDefault.
void ReadVec(const json& j, Math::FloatVector4& out, float wDefault = 0.0f)
{
    if (!j.is_array()) return;
    const size_t n = j.size();
    out = Math::FloatVector4(
        n > 0 ? j[0].get<float>() : 0.0f,
        n > 1 ? j[1].get<float>() : 0.0f,
        n > 2 ? j[2].get<float>() : 0.0f,
        n > 3 ? j[3].get<float>() : wDefault);
}

template <typename T>
void ReadField(const json& j, const char* key, T& out)
{
    auto it = j.find(key);
    if (it != j.end() && !it->is_null())
        out = it->get<T>();
}

void ReadBiome(const json& j, SceneBiome& b)
{
    ReadField(j, "sandMaxMeters",  b.SandMaxMeters);
    ReadField(j, "sandFadeMeters", b.SandFadeMeters);
    ReadField(j, "snowMinMeters",  b.SnowMinMeters);
    ReadField(j, "snowFadeMeters", b.SnowFadeMeters);
    ReadField(j, "slopeRockMin",   b.SlopeRockMin);
    ReadField(j, "slopeRockMax",   b.SlopeRockMax);
}

void ReadMaterial(const json& j, SceneMaterial& m, const std::filesystem::path& baseDir)
{
    std::string albedoStr, ormStr;
    ReadField(j, "atlasAlbedo",        albedoStr);
    ReadField(j, "atlasORM",           ormStr);
    ReadField(j, "atlasRepeatMeters",  m.AtlasRepeatMeters);
    if (!albedoStr.empty()) m.AtlasAlbedo = ResolvePath(albedoStr, baseDir);
    if (!ormStr.empty())    m.AtlasORM    = ResolvePath(ormStr,    baseDir);
    auto bIt = j.find("biome");
    if (bIt != j.end() && bIt->is_object())
        ReadBiome(*bIt, m.Biome);
}

void ReadTile(const json& j, SceneTile& t, const std::filesystem::path& baseDir)
{
    std::string heightmapStr;
    ReadField(j, "heightmap",   heightmapStr);
    ReadField(j, "dxy",         t.Dxy);
    ReadField(j, "heightScale", t.HeightScale);
    ReadField(j, "heightBias",  t.HeightBias);
    if (!heightmapStr.empty()) t.Heightmap = ResolvePath(heightmapStr, baseDir);
    auto oIt = j.find("originXY");
    if (oIt != j.end() && oIt->is_array() && oIt->size() >= 2)
    {
        t.OriginX = (*oIt)[0].get<float>();
        t.OriginY = (*oIt)[1].get<float>();
    }
}

void ReadCamera(const json& j, SceneCamera& c)
{
    auto pIt = j.find("startPositionXYZ");
    if (pIt != j.end()) ReadVec(*pIt, c.StartPosition);
    auto lIt = j.find("startLookAtXYZ");
    if (lIt != j.end()) ReadVec(*lIt, c.StartLookAt);
    ReadField(j, "fovDegrees", c.FovDegrees);
}

} // anonymous namespace

void ApplyDefaults(SceneConfig& cfg)
{
    cfg.Name = "default";

    SceneTile t;
    t.Heightmap   = "assets/CanvasTerrainViewer/default_heightmap.png";
    t.Dxy         = 1.0f;
    t.HeightScale = 64.0f;
    cfg.Tiles.clear();
    cfg.Tiles.push_back(t);

    cfg.Material.AtlasAlbedo       = "assets/CanvasTerrainViewer/terrain_atlas_albedo.png";
    cfg.Material.AtlasORM          = "assets/CanvasTerrainViewer/terrain_atlas_orm.png";
    cfg.Material.AtlasRepeatMeters = 8.0f;
    cfg.Material.Biome             = SceneBiome{};

    cfg.Camera = SceneCamera{};
}

bool LoadSceneConfig(
    const std::filesystem::path& path,
    SceneConfig*                 outConfig,
    XLogger*                     pLogger)
{
    if (!outConfig)
        return false;

    std::ifstream f(path);
    if (!f.is_open())
    {
        LogError(pLogger, "LoadSceneConfig: cannot open '%s'", path.string().c_str());
        return false;
    }

    json j;
    try
    {
        // Permissive parse: allow JSON5-style // and /* */ comments so users
        // can annotate their scene files.
        j = json::parse(f, /*cb*/ nullptr, /*allow_exceptions*/ true, /*ignore_comments*/ true);
    }
    catch (const json::parse_error& e)
    {
        LogError(pLogger, "LoadSceneConfig: parse error in '%s' at byte %zu: %s",
            path.string().c_str(), static_cast<size_t>(e.byte), e.what());
        return false;
    }

    const std::filesystem::path baseDir = path.parent_path();

    auto sIt = j.find("scene");
    if (sIt != j.end() && sIt->is_object())
        ReadField(*sIt, "name", outConfig->Name);

    auto tIt = j.find("tiles");
    if (tIt != j.end() && tIt->is_array())
    {
        outConfig->Tiles.clear();
        outConfig->Tiles.reserve(tIt->size());
        for (const auto& tj : *tIt)
        {
            SceneTile t;
            ReadTile(tj, t, baseDir);
            outConfig->Tiles.push_back(std::move(t));
        }
    }

    auto mIt = j.find("material");
    if (mIt != j.end() && mIt->is_object())
        ReadMaterial(*mIt, outConfig->Material, baseDir);

    auto cIt = j.find("camera");
    if (cIt != j.end() && cIt->is_object())
        ReadCamera(*cIt, outConfig->Camera);

    LogInfo(pLogger, "LoadSceneConfig: loaded scene '%s' (%zu tile(s)) from '%s'",
        outConfig->Name.c_str(), outConfig->Tiles.size(), path.string().c_str());
    return true;
}

} // namespace TerrainViewer
} // namespace Canvas
