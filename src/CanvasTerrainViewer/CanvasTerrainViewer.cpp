// CanvasTerrainViewer.cpp
//
// Entry point + application shell for the Canvas terrain viewer.
//
// v1 milestone: load a heightfield bitmap (any WIC-decodable format), build a
// CPU triangulated grid mesh from it, render with the existing PBR deferred
// path, drive a 5-minute day/night cycle (sun + moon directional lights), and
// expose FPS-style camera controls plus a small HUD.

#include "pch.h"
#include "HeightField.h"
#include "SceneConfig.h"
#include "SkyCubeLoader.h"
#include "TerrainMaterial.h"
#include "QLogAdapter.h"
#include "TokenParser.h"
#include "InCommand.h"

//-------------------------------------------------------------------------------------------------
// D3D12 Agility SDK exports - required to activate newer D3D12 APIs.
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 616; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

namespace
{

//-------------------------------------------------------------------------------------------------
// Optional dedicated console window for live log output.
class CConsole
{
    FILE* m_stdout = nullptr;
    FILE* m_stderr = nullptr;

public:
    CConsole()
    {
        AllocConsole();
        SetConsoleTitleA("CanvasTerrainViewer - Log");
        freopen_s(&m_stdout, "CONOUT$", "w", stdout);
        freopen_s(&m_stderr, "CONOUT$", "w", stderr);
    }
    ~CConsole()
    {
        if (m_stdout) fclose(m_stdout);
        if (m_stderr) fclose(m_stderr);
        FreeConsole();
    }
};

//-------------------------------------------------------------------------------------------------
class CLogSink : public QLog::Sink
{
    std::ofstream m_logFile;
    bool m_consoleOutput;

public:
    CLogSink(const std::filesystem::path& logFilePath, bool consoleOutput)
        : m_consoleOutput(consoleOutput)
    {
        if (!logFilePath.empty())
            m_logFile.open(logFilePath, std::ios::out | std::ios::trunc);
    }

    void Write(const QLog::Message& message) override
    {
        std::ostringstream oss;
        oss << QLog::FormatTimestamp(message) << QLog::ToString(message.level)
            << ": " << message.text << '\n';
        std::string formatted = oss.str();

        OutputDebugStringA(formatted.c_str());
        if (m_logFile.is_open())
        {
            m_logFile << formatted;
            m_logFile.flush();
        }
        if (m_consoleOutput)
            fputs(formatted.c_str(), stdout);
    }
};

//-------------------------------------------------------------------------------------------------
// Day/night cycle helper.
//
// Drives two polar-opposite directional lights (sun, moon) plus an ambient
// fill from a single phase angle theta = 2*pi * t / cycleSeconds. Sun direction
// is the *light direction* (i.e., pointing away from the surface toward the
// "source"), which Canvas reads from the attached node's row-0 forward axis.
class CDayNightCycle
{
public:
    static constexpr float kDefaultCycleSeconds = 300.0f; // 5 minutes
    static constexpr float kSunPeakIntensity    = 4.0f;   // tuned for HDR lit scene
    static constexpr float kMoonPeakIntensity   = kSunPeakIntensity / 40.0f;

    void Initialize(
        Canvas::XCanvas*           pCanvas,
        Canvas::XScene*       pScene)
    {
        Gem::ThrowGemError(pCanvas->CreateLight(Canvas::LightType::Directional, &m_pSun, "Sun"));
        Gem::ThrowGemError(pCanvas->CreateLight(Canvas::LightType::Directional, &m_pMoon, "Moon"));
        Gem::ThrowGemError(pCanvas->CreateLight(Canvas::LightType::Ambient,     &m_pAmbient, "Ambient"));

        m_pSun->SetFlags(Canvas::LightFlags::Enabled | Canvas::LightFlags::CastsShadows);
        m_pMoon->SetFlags(Canvas::LightFlags::Enabled | Canvas::LightFlags::CastsShadows);
        m_pAmbient->SetFlags(Canvas::LightFlags::Enabled);

        // Per-light shadow resolution + bias.  The shadow frustum extent
        // depends on the loaded terrain (heightScale drives shadow tail
        // length at low sun angles) and is configured later via
        // XLight::SetDirectionalShadowAutoFit once LoadTerrain has run.
        // Until that runs the XLight defaults apply.
        for (auto* pDirLight : { m_pSun.Get(), m_pMoon.Get() })
        {
            pDirLight->SetShadowResolution(2048);
            pDirLight->SetShadowDepthBias(1e-4f, 2.0f, 0.5f);
        }

        Gem::ThrowGemError(pCanvas->CreateSceneGraphNode(&m_pSunNode,  "SunNode"));
        Gem::ThrowGemError(pCanvas->CreateSceneGraphNode(&m_pMoonNode, "MoonNode"));
        Gem::ThrowGemError(pCanvas->CreateSceneGraphNode(&m_pAmbientNode, "AmbientNode"));

        m_pSunNode->BindElement(m_pSun);
        m_pMoonNode->BindElement(m_pMoon);
        m_pAmbientNode->BindElement(m_pAmbient);

        Canvas::XSceneGraphNode* pRoot = pScene->GetRootNode();
        pRoot->AddChild(m_pSunNode);
        pRoot->AddChild(m_pMoonNode);
        pRoot->AddChild(m_pAmbientNode);

        // Initial state - start at a sunrise-ish angle so the user sees colored
        // lighting on the first frame.  TimeString interprets phase=0.25 as
        // 06:00, and Update's sun-position math now puts the sun at the +X
        // horizon there, so this lands on the dawn crossfade as intended.
        m_TimeOfDay = 0.25f * kDefaultCycleSeconds; // ~6:00 AM equivalent
        Update(0.0f);
    }

    void SetCycleSeconds(float s) { m_CycleSeconds = s > 1.0f ? s : 1.0f; }
    void SetPaused(bool p)        { m_Paused = p; }
    void TogglePaused()           { m_Paused = !m_Paused; }
    void ScrubMinutes(float mins)
    {
        // Treat one full cycle as 24 in-world hours.
        const float secondsPerInWorldMin = m_CycleSeconds / (24.0f * 60.0f);
        m_TimeOfDay += mins * secondsPerInWorldMin;
        // Wrap into [0, cycleSeconds)
        m_TimeOfDay = std::fmod(m_TimeOfDay, m_CycleSeconds);
        if (m_TimeOfDay < 0.0f) m_TimeOfDay += m_CycleSeconds;
    }

    // Advance time and refresh the sun/moon/ambient state.
    void Update(float dtime)
    {
        if (!m_Paused)
            m_TimeOfDay = std::fmod(m_TimeOfDay + dtime, m_CycleSeconds);

        const float theta = (m_TimeOfDay / m_CycleSeconds)
                          * static_cast<float>(2.0 * Canvas::Math::Pi);
        // Sun's apparent position in the sky, parameterised so the phase
        // values match the TimeString clock:
        //   theta = 0    (00:00, midnight) -> sun at nadir   (0, 0, -1)
        //   theta = pi/2 (06:00, sunrise)  -> sun at +X      (1, 0,  0)
        //   theta = pi   (12:00, noon)     -> sun at zenith  (0, 0,  1)
        //   theta = 3pi/2 (18:00, sunset)  -> sun at -X      (-1, 0, 0)
        // The deferred shader treats the directional light's basis-row-0
        // forward as the *direction photons travel* (it negates it to
        // compute L), so we point the light *away* from the sun's
        // position.  Moon is the polar opposite.
        const Canvas::Math::FloatVector4 sunPos (
            std::sin(theta), 0.0f, -std::cos(theta), 0.0f);
        const Canvas::Math::FloatVector4 sunDir  = -sunPos;  // photons travel away from sun
        const Canvas::Math::FloatVector4 moonDir =  sunPos;  // polar opposite
        const Canvas::Math::FloatVector4 worldUp(0.0f, 0.0f, 1.0f, 0.0f);

        OrientLightNode(m_pSunNode,  sunDir,  worldUp);
        OrientLightNode(m_pMoonNode, moonDir, worldUp);

        // Intensity fades smoothly across the horizon.  sunPos.z is the
        // sun's height above the horizon plane (positive = above, negative
        // = below); smoothstep(-0.1, 0.2, sunPos.z) gives a gentle
        // dawn/dusk crossfade where both lights contribute.
        const float sunHeight = sunPos.Z;
        const float sunGate  = SmoothStep(-0.1f, 0.2f,  sunHeight);
        const float moonGate = SmoothStep(-0.1f, 0.2f, -sunHeight);

        // Warm sun: yellowish-white. Color tints toward orange near horizon.
        const float horizonness = 1.0f - std::min(1.0f, std::abs(sunHeight) * 2.5f);
        const Canvas::Math::FloatVector4 sunDay   (1.00f, 0.95f, 0.80f, 1.0f);
        const Canvas::Math::FloatVector4 sunDawn  (1.00f, 0.55f, 0.25f, 1.0f);
        const Canvas::Math::FloatVector4 sunColor = Lerp(sunDay, sunDawn, horizonness * sunGate);

        m_pSun->SetColor(sunColor);
        m_pSun->SetIntensity(kSunPeakIntensity * sunGate);

        const Canvas::Math::FloatVector4 moonColor(0.55f, 0.65f, 0.95f, 1.0f);
        m_pMoon->SetColor(moonColor);
        m_pMoon->SetIntensity(kMoonPeakIntensity * moonGate);

        // Ambient: a non-trivial floor so back-facing slopes stay readable.
        // Day ambient is roughly sunIntensity / 8; night ambient is moon-anchored
        // with a small irreducible floor so totally moonless scenes still show
        // shape against the sky.
        const float ambDay     = 0.45f * sunGate;
        const float ambNight   = 0.10f * moonGate;
        const float ambFloor   = 0.05f;
        const Canvas::Math::FloatVector4 ambDayColor (0.55f, 0.65f, 0.85f, 1.0f);
        const Canvas::Math::FloatVector4 ambNightColor(0.15f, 0.20f, 0.35f, 1.0f);
        const float ambBlend = sunGate / std::max(0.001f, sunGate + moonGate);
        const Canvas::Math::FloatVector4 ambColor = Lerp(ambNightColor, ambDayColor, ambBlend);
        m_pAmbient->SetColor(ambColor);
        m_pAmbient->SetIntensity(ambDay + ambNight + ambFloor);

        // Cache display-side state for the composite background to read.
        // sunPos is the direction toward the sun in the sky (same vector
        // the procedural composite shader expects via SunDirection); the
        // moon is the polar opposite.  Display colours pack intensity
        // (sunGate / moonGate) into .a so a single field carries both
        // tint and brightness.  Stars fade out during the day by gating
        // on moonGate (which is 1 when the sun is below the horizon).
        m_SunPos  = sunPos;
        m_MoonPos = -sunPos;
        m_SunDisplayColor  = Canvas::Math::FloatVector4(
            sunColor.X,  sunColor.Y,  sunColor.Z,  kSunPeakIntensity * sunGate);
        // Moon disc display colour is the moon's visual appearance (very
        // pale neutral white), NOT the cooler moonColor used above for
        // the directional light's tint of the scene.  Moonlight is the
        // sun's light reflected off a near-grey surface and arrives at
        // the eye after a Rayleigh-scattering bias toward blue, hence
        // the cool tint for the *lighting*; but the moon disc itself
        // looks roughly white-grey.
        m_MoonDisplayColor = Canvas::Math::FloatVector4(
            0.95f, 0.95f, 0.92f, moonGate);
        m_StarsIntensity = moonGate;
    }

    // 24-hour clock string: 00:00 at theta=0 (midnight), 06:00 at theta=pi/2, etc.
    std::string TimeString() const
    {
        const float frac     = m_TimeOfDay / m_CycleSeconds;
        const float hours24  = frac * 24.0f;
        const int   hours    = static_cast<int>(hours24) % 24;
        const int   minutes  = static_cast<int>((hours24 - std::floor(hours24)) * 60.0f);
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", hours, minutes);
        return buf;
    }

    bool IsPaused() const { return m_Paused; }

    // Direct access to the directional lights.  Used by the application
    // when it needs to apply per-light state that isn't part of the
    // day/night cycle itself.
    Canvas::XLight* GetSunLight()  const { return m_pSun.Get(); }
    Canvas::XLight* GetMoonLight() const { return m_pMoon.Get(); }

    // Time-of-day phase in [0, 1).  0 = midnight, 0.25 = sunrise, 0.5 = noon,
    // 0.75 = sunset.  Used by the sky preset selector to crossfade between
    // authored skybox cubemaps.
    float GetPhase() const
    {
        const float p = m_TimeOfDay / m_CycleSeconds;
        return p - std::floor(p);
    }

    // Display-side accessors used by the composite background:
    //   GetSunPosition / GetMoonPosition return the unit world vector
    //   pointing from the viewer TOWARD the body (opposite of the
    //   matching directional light's photon direction).
    //   Get*DisplayColor returns rgb tint with intensity baked into .a;
    //   GetStarsIntensity is 1 at night, fades to 0 around dawn/dusk.
    Canvas::Math::FloatVector4 GetSunPosition()      const { return m_SunPos; }
    Canvas::Math::FloatVector4 GetMoonPosition()     const { return m_MoonPos; }
    Canvas::Math::FloatVector4 GetSunDisplayColor()  const { return m_SunDisplayColor; }
    Canvas::Math::FloatVector4 GetMoonDisplayColor() const { return m_MoonDisplayColor; }
    float                      GetStarsIntensity()   const { return m_StarsIntensity; }

private:
    static float SmoothStep(float edge0, float edge1, float x)
    {
        const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    static Canvas::Math::FloatVector4 Lerp(
        const Canvas::Math::FloatVector4& a,
        const Canvas::Math::FloatVector4& b,
        float t)
    {
        return Canvas::Math::FloatVector4(
            a[0] + (b[0] - a[0]) * t,
            a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t,
            a[3] + (b[3] - a[3]) * t);
    }

    static void OrientLightNode(
        Canvas::XSceneGraphNode*         pNode,
        const Canvas::Math::FloatVector4& forward,
        const Canvas::Math::FloatVector4& worldUp)
    {
        Canvas::Math::FloatMatrix4x4 rot = Canvas::Math::IdentityMatrix<float, 4, 4>();
        rot[0] = forward;
        // If forward is collinear with worldUp, fall back to a stable up.
        Canvas::Math::FloatVector4 upRef = worldUp;
        if (std::abs(forward[2]) > 0.999f)
            upRef = Canvas::Math::FloatVector4(0.0f, 1.0f, 0.0f, 0.0f);
        Canvas::Math::ComposePointToBasisVectors(upRef, forward, rot[1], rot[2]);
        pNode->SetLocalRotation(Canvas::Math::QuaternionFromRotationMatrix(rot));
    }

    Gem::TGemPtr<Canvas::XLight>          m_pSun;
    Gem::TGemPtr<Canvas::XLight>          m_pMoon;
    Gem::TGemPtr<Canvas::XLight>          m_pAmbient;
    Gem::TGemPtr<Canvas::XSceneGraphNode> m_pSunNode;
    Gem::TGemPtr<Canvas::XSceneGraphNode> m_pMoonNode;
    Gem::TGemPtr<Canvas::XSceneGraphNode> m_pAmbientNode;

    // Cached display-side state produced by Update for the composite
    // background to consume each frame.  Sun/moon positions are the
    // sky-facing directions (toward the body), display colours pack
    // intensity into .a, and stars intensity fades to zero during day.
    // Kept together up front to avoid alignment padding between the
    // alignas(16) FloatVector4 members and the trailing scalars.
    Canvas::Math::FloatVector4 m_SunPos          { 0.0f, 0.0f, -1.0f, 0.0f };
    Canvas::Math::FloatVector4 m_MoonPos         { 0.0f, 0.0f,  1.0f, 0.0f };
    Canvas::Math::FloatVector4 m_SunDisplayColor { 1.0f, 1.0f, 1.0f, 0.0f };
    Canvas::Math::FloatVector4 m_MoonDisplayColor{ 1.0f, 1.0f, 1.0f, 0.0f };

    float m_CycleSeconds   = kDefaultCycleSeconds;
    float m_TimeOfDay      = 0.0f;
    float m_StarsIntensity = 0.0f;
    bool  m_Paused         = false;
};

} // anonymous namespace

//-------------------------------------------------------------------------------------------------
class CTerrainApp
{
    std::string m_Title;
    HINSTANCE   m_hInstance;
    Gem::TGemPtr<Canvas::XLogger>           m_pLogger;
    Gem::TGemPtr<Canvas::Platform::Win32::XAppWindow> m_pWindow;
    Gem::TGemPtr<Canvas::Platform::Win32::XRawInput>  m_pInput;
    Gem::TGemPtr<Canvas::XCanvas>           m_pCanvas;
    Gem::TGemPtr<Canvas::XScene>       m_pScene;
    Gem::TGemPtr<Canvas::XCamera>           m_pCamera;
    Gem::TGemPtr<Canvas::XCanvasPlugin>     m_pGfxPlugin;
    Gem::TGemPtr<Canvas::XGfxDevice>        m_pGfxDevice;
    Gem::TGemPtr<Canvas::XGfxSwapChain>     m_pGfxSwapChain;
    Gem::TGemPtr<Canvas::XGfxRenderQueue>   m_pGfxRenderQueue;
    Gem::TGemPtr<Canvas::XGfxSurface>       m_pHeightmapSurface;
    Gem::TGemPtr<Canvas::XGfxSurface>       m_pSkyCubeDay;
    Gem::TGemPtr<Canvas::XGfxSurface>       m_pSkyCubeDusk;
    Gem::TGemPtr<Canvas::XGfxSurface>       m_pSkyCubeNight;
    Gem::TGemPtr<Canvas::XGfxSurface>       m_pSkyCubeStars;
    Gem::TGemPtr<Canvas::XGfxSurface>       m_pMoonTexture;
    Gem::TGemPtr<Canvas::XGfxMaterial>      m_pTerrainMaterial;
    Gem::TGemPtr<Canvas::XGfxMeshData>      m_pTerrainPatchMesh;
    Gem::TGemPtr<Canvas::XMeshInstance>     m_pTerrainInstance;
    Gem::TGemPtr<Canvas::XSceneGraphNode>   m_pTerrainNode;
    Gem::TGemPtr<Canvas::XFont>             m_pFont;
    Gem::TGemPtr<Canvas::XFont>             m_pFontMono;
    Gem::TGemPtr<Canvas::XUIGraph>          m_pUIGraph;
    Gem::TGemPtr<Canvas::XUIRectElement>    m_pHudPanel;
    Gem::TGemPtr<Canvas::XUITextElement>    m_pTitleText;
    Gem::TGemPtr<Canvas::XUITextElement>    m_pStatusText;

    CDayNightCycle    m_DayNight;
    int               m_exitFrameCount;
    bool              m_logFps;
    float             m_fps = 0.0f;
    std::string       m_statusString;
    Canvas::TerrainViewer::SceneConfig m_SceneConfig;
    float             m_CycleSeconds = CDayNightCycle::kDefaultCycleSeconds;

    float m_CameraYaw   = 0.0f;
    float m_CameraPitch = 0.0f;

    bool LoadTerrain()
    {
        if (m_SceneConfig.Tiles.empty())
        {
            Canvas::LogError(m_pLogger.Get(), "Scene has no tiles");
            return false;
        }
        const Canvas::TerrainViewer::SceneTile& tile = m_SceneConfig.Tiles[0];

        if (tile.Heightmap.empty())
        {
            Canvas::LogError(m_pLogger.Get(), "Scene tile has no heightmap path");
            return false;
        }
        if (!std::filesystem::exists(tile.Heightmap))
        {
            Canvas::LogError(m_pLogger.Get(), "Heightmap file not found: '%s'",
                tile.Heightmap.string().c_str());
            return false;
        }

        Canvas::HeightField::LoadOptions loadOpts;
        loadOpts.DxyMeters   = tile.Dxy;
        loadOpts.HeightScale = tile.HeightScale;
        loadOpts.HeightBias  = tile.HeightBias;

        Canvas::HeightField::HeightField field;
        const std::wstring widePath = tile.Heightmap.wstring();
        if (!Canvas::HeightField::LoadHeightFieldWIC(
                widePath.c_str(), loadOpts, &field, m_pLogger.Get()))
        {
            Canvas::LogError(m_pLogger.Get(), "Failed to load heightmap '%s'",
                tile.Heightmap.string().c_str());
            return false;
        }

        const Canvas::TerrainViewer::SceneMaterial& mat = m_SceneConfig.Material;
        if (mat.AtlasAlbedo.empty() || mat.AtlasORM.empty())
        {
            Canvas::LogError(m_pLogger.Get(), "Scene material is missing atlas paths");
            return false;
        }

        Canvas::Platform::Win32::ImageData albedoAtlas, ormAtlas;
        if (!Canvas::Platform::Win32::LoadImageData(mat.AtlasAlbedo.wstring().c_str(),
                Canvas::GfxFormat::R8G8B8A8_UNorm, &albedoAtlas, m_pLogger.Get()))
        {
            Canvas::LogError(m_pLogger.Get(), "Failed to load atlas albedo '%s'",
                mat.AtlasAlbedo.string().c_str());
            return false;
        }
        if (!Canvas::Platform::Win32::LoadImageData(mat.AtlasORM.wstring().c_str(),
                Canvas::GfxFormat::R8G8B8A8_UNorm, &ormAtlas, m_pLogger.Get()))
        {
            Canvas::LogError(m_pLogger.Get(), "Failed to load atlas ORM '%s'",
                mat.AtlasORM.string().c_str());
            return false;
        }

        Canvas::TerrainViewer::TerrainMaterialOptions matOpts;
        matOpts.OriginX           = tile.OriginX - 0.5f * field.WorldWidth();
        matOpts.OriginY           = tile.OriginY - 0.5f * field.WorldHeight();
        matOpts.AtlasRepeatMeters = mat.AtlasRepeatMeters;
        matOpts.SandMaxMeters     = mat.Biome.SandMaxMeters;
        matOpts.SandFadeMeters    = mat.Biome.SandFadeMeters;
        matOpts.SnowMinMeters     = mat.Biome.SnowMinMeters;
        matOpts.SnowFadeMeters    = mat.Biome.SnowFadeMeters;
        matOpts.SlopeRockMin      = mat.Biome.SlopeRockMin;
        matOpts.SlopeRockMax      = mat.Biome.SlopeRockMax;

        Canvas::TerrainViewer::TerrainMaterialOutputs matOutputs;
        Gem::Result mr = Canvas::TerrainViewer::BuildTerrainMaterial(
            m_pGfxDevice, field, albedoAtlas, ormAtlas, matOpts, &matOutputs, m_pLogger.Get());
        if (Gem::Failed(mr))
        {
            Canvas::LogError(m_pLogger.Get(),
                "BuildTerrainMaterial failed: %s", GemResultString(mr));
            return false;
        }
        // BuildTerrainMaterial uploads the atlases via the device copy queue
        // but leaves them in LAYOUT_COMMON. Schedule the COMMON ->
        // SHADER_RESOURCE finalization so the terrain PS can sample them
        // through DATA_STATIC SRVs.
        Gem::ThrowGemError(m_pGfxRenderQueue->FinalizeUploadAsShaderResource(matOutputs.pAlbedo));
        Gem::ThrowGemError(m_pGfxRenderQueue->FinalizeUploadAsShaderResource(matOutputs.pAO));
        Gem::ThrowGemError(m_pGfxRenderQueue->FinalizeUploadAsShaderResource(matOutputs.pRoughness));

        // Upload the heightmap into an R16_UNorm 2D surface; the GPU
        // tessellation pipeline's DS samples it to lift patch vertices into Z.
        {
            Canvas::GfxSurfaceDesc hd = Canvas::GfxSurfaceDesc::SurfaceDesc2D(
                Canvas::GfxFormat::R16_UNorm,
                field.Desc.Width, field.Desc.Height,
                Canvas::SurfaceFlag_ShaderResource);
            Gem::ThrowGemError(m_pGfxDevice->CreateSurface(hd, &m_pHeightmapSurface));
            const uint32_t rowPitchBytes = field.Desc.Width * sizeof(uint16_t);
            Gem::ThrowGemError(m_pGfxDevice->UploadTextureRegion(
                m_pHeightmapSurface, 0, 0, 0,
                field.Desc.Width, field.Desc.Height,
                field.Samples.data(), rowPitchBytes));
            // Schedule the COMMON -> SHADER_RESOURCE transition on the
            // render queue so the heightmap is sampleable by the terrain
            // tessellation pipeline. The render queue stamps the surface
            // with a fence token; the terrain drain waits for retirement
            // before binding (terrain root sig uses DATA_STATIC SRVs).
            Gem::ThrowGemError(m_pGfxRenderQueue->FinalizeUploadAsShaderResource(
                m_pHeightmapSurface));
        }

        // Build the terrain material: standard albedo / AO / roughness
        // textures plus a displacement extension that carries the heightmap
        // and the tessellation-LOD knobs. The engine picks the
        // displacement render path automatically when DrawMesh sees a
        // procedural-patch mesh whose material has displacement attached.
        Gem::ThrowGemError(m_pGfxDevice->CreateMaterial(&m_pTerrainMaterial));
        m_pTerrainMaterial->SetTexture(Canvas::MaterialLayerRole::Albedo,           matOutputs.pAlbedo);
        m_pTerrainMaterial->SetTexture(Canvas::MaterialLayerRole::AmbientOcclusion, matOutputs.pAO);
        m_pTerrainMaterial->SetTexture(Canvas::MaterialLayerRole::Roughness,        matOutputs.pRoughness);

        Canvas::GfxDisplacementDesc disp = {};
        disp.pDisplacementMap  = m_pHeightmapSurface;
        disp.MapScale          = field.Desc.HeightScale;
        disp.MapBias           = field.Desc.HeightBias;
        disp.MinTessFactor     = 2.0f;
        disp.MaxTessFactor     = 32.0f;
        disp.DistanceLodScale  = 10.0f;
        disp.CurvatureLodScale = 0.5f;
        m_pTerrainMaterial->SetDisplacement(&disp);

        // Patch-grid mesh: explicit per-CP positions (mesh-local XY,
        // Z=0), UVs (in D3D texture-UV space; v=0 = image top), and
        // base normals (all +Z for a flat XY-plane patch grid -- the
        // DS displaces each CP along this normal by the decoded sample,
        // which collapses to the classic Z-only heightfield lift).
        // Per patch the four CPs are emitted in HS quad order:
        // 0=(0,0), 1=(1,0), 2=(1,1), 3=(0,1) in cpLocal parameter
        // space.  The (1 - vBase) flip on the V axis preserves the
        // existing "image-top renders at Canvas-left (high worldY)"
        // convention.
        const uint32_t patchesPerSide = 64;
        const float    meshExtentX    = field.WorldWidth();
        const float    meshExtentY    = field.WorldHeight();
        const float    invN           = 1.0f / static_cast<float>(patchesPerSide);

        std::vector<Canvas::Math::FloatVector4> patchPositions;
        std::vector<Canvas::Math::FloatVector2> patchUVs;
        std::vector<Canvas::Math::FloatVector4> patchNormals;
        const size_t kCPCount = static_cast<size_t>(patchesPerSide) * patchesPerSide * 4u;
        patchPositions.reserve(kCPCount);
        patchUVs     .reserve(kCPCount);
        patchNormals .reserve(kCPCount);

        static const float kCornerOffsets[4][2] =
        {
            { 0.0f, 0.0f },   // CP 0
            { 1.0f, 0.0f },   // CP 1
            { 1.0f, 1.0f },   // CP 2
            { 0.0f, 1.0f },   // CP 3
        };

        for (uint32_t py = 0; py < patchesPerSide; ++py)
        {
            for (uint32_t px = 0; px < patchesPerSide; ++px)
            {
                for (int c = 0; c < 4; ++c)
                {
                    const float lu = (static_cast<float>(px) + kCornerOffsets[c][0]) * invN;
                    const float lv = (static_cast<float>(py) + kCornerOffsets[c][1]) * invN;
                    patchPositions.push_back(Canvas::Math::FloatVector4(
                        lu * meshExtentX, lv * meshExtentY, 0.0f, 1.0f));
                    patchUVs.push_back(Canvas::Math::FloatVector2(
                        lu, 1.0f - lv));
                    patchNormals.push_back(Canvas::Math::FloatVector4(
                        0.0f, 0.0f, 1.0f, 0.0f));
                }
            }
        }

        // Pre-displacement patch grid footprint in mesh-local XY; the
        // engine inflates the bounds by the material's displacement
        // magnitude.
        const Canvas::Math::AABB tileBounds(
            Canvas::Math::FloatVector4(0.0f,        0.0f,        0.0f, 0.0f),
            Canvas::Math::FloatVector4(meshExtentX, meshExtentY, 0.0f, 0.0f));

        Canvas::MeshDataGroupDesc group = {};
        group.VertexCount = static_cast<uint32_t>(patchPositions.size());
        group.pPositions  = patchPositions.data();
        group.pNormals    = patchNormals.data();
        group.pUV0        = patchUVs.data();
        group.pMaterial   = m_pTerrainMaterial;

        Canvas::MeshDataDesc meshDesc = {};
        meshDesc.pGroups     = &group;
        meshDesc.GroupCount  = 1;
        meshDesc.pName       = "TerrainPatchMesh_64x64";
        meshDesc.Topology    = Canvas::GfxPrimitiveTopology::PatchList4CP;
        meshDesc.LocalBounds = tileBounds;

        Gem::ThrowGemError(m_pGfxDevice->CreateMeshData(meshDesc, &m_pTerrainPatchMesh));

        char tileName[64];
        snprintf(tileName, sizeof(tileName), "Tile_%d_%d",
            static_cast<int>(std::round(matOpts.OriginX / std::max(1.0f, field.WorldWidth()))),
            static_cast<int>(std::round(matOpts.OriginY / std::max(1.0f, field.WorldHeight()))));
        Gem::ThrowGemError(m_pCanvas->CreateMeshInstance(&m_pTerrainInstance, tileName));
        m_pTerrainInstance->SetMeshData(m_pTerrainPatchMesh);

        Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&m_pTerrainNode, "TerrainNode"));
        // Only translation on the node - the tile's world dimensions live
        // on the material's displacement desc so children attached to this
        // node (e.g. trees, markers) don't inherit a 1024x scale factor.
        m_pTerrainNode->SetLocalTranslation(
            Canvas::Math::FloatVector4{ matOpts.OriginX, matOpts.OriginY, 0.0f, 1.0f });
        m_pTerrainNode->BindElement(m_pTerrainInstance);
        m_pScene->GetRootNode()->AddChild(m_pTerrainNode);

        // Camera start pose from the scene config.
        const Canvas::TerrainViewer::SceneCamera& sc = m_SceneConfig.Camera;
        const float halfDiag = 0.5f * std::sqrt(
            field.WorldWidth()  * field.WorldWidth() +
            field.WorldHeight() * field.WorldHeight());

        if (m_pCamera)
        {
            m_pCamera->SetFovAngle(sc.FovDegrees * static_cast<float>(Canvas::Math::Pi / 180.0));
            // Far clip large enough to see the whole tile from any reasonable
            // start vantage.
            const float farClip = std::max(1000.0f, halfDiag * 6.0f);
            m_pCamera->SetFarClip(farClip);
        }

        if (auto* pCamNode = m_pCamera ? m_pCamera->GetAttachedNode() : nullptr)
        {
            pCamNode->SetLocalTranslation(sc.StartPosition);

            Canvas::Math::FloatVector4 forward = (sc.StartLookAt - sc.StartPosition).Normalize();
            Canvas::Math::FloatVector4 worldUp(0.0f, 0.0f, 1.0f, 0.0f);
            Canvas::Math::FloatMatrix4x4 rot = Canvas::Math::IdentityMatrix<float, 4, 4>();
            rot[0] = forward;
            Canvas::Math::ComposePointToBasisVectors(worldUp, forward, rot[1], rot[2]);
            pCamNode->SetLocalRotation(Canvas::Math::QuaternionFromRotationMatrix(rot));

            m_CameraYaw   = std::atan2(forward[1], forward[0]);
            m_CameraPitch = std::asin(std::clamp(forward[2], -1.0f, 1.0f));
        }

        Canvas::LogInfo(m_pLogger.Get(),
            "Terrain ready: scene='%s' tile=%u x %u texels (%.1f x %.1f m), heightScale=%.1fm",
            m_SceneConfig.Name.c_str(),
            field.Desc.Width, field.Desc.Height,
            field.WorldWidth(), field.WorldHeight(),
            field.Desc.HeightScale);

        // Build the heightfield mip chain. v2 GPU tessellation will sample
        // this in the HS for stable curvature LOD; for v1 we only validate
        // that the chain builds and log its shape so any regression is
        // visible in the load log.
        std::vector<Canvas::HeightField::HeightMipLevel> mips;
        if (Canvas::HeightField::BuildMipChain(field, &mips) && !mips.empty())
        {
            Canvas::LogInfo(m_pLogger.Get(),
                "Heightfield mip chain: %zu levels (mip 0 = %ux%u, mip %zu = %ux%u)",
                mips.size(),
                mips.front().Width, mips.front().Height,
                mips.size() - 1,
                mips.back().Width, mips.back().Height);
        }
        else
        {
            Canvas::LogWarn(m_pLogger.Get(), "BuildMipChain failed; v2 curvature LOD will lack mip-matched sampling");
        }
        return true;
    }

    // Load the three skybox cubemap presets (day, dusk, night) from the same
    // assets directory as the heightmap, and schedule the COMMON ->
    // SHADER_RESOURCE transitions so the composite pass can sample them.  The
    // per-frame UpdateSkyBackground picks an adjacent pair + crossfade factor
    // by time of day and pushes it into XScene::SetBackground.
    bool LoadSky()
    {
        // Sky assets live next to the rest of the bundled tile assets
        // (staged into bin/assets/CanvasTerrainViewer/sky/).
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        const std::filesystem::path skyDir =
            std::filesystem::path(exePath).parent_path()
            / "assets" / "CanvasTerrainViewer" / "sky";

        auto loadOne = [&](const char* name, Gem::TGemPtr<Canvas::XGfxSurface>& out) -> bool
        {
            Canvas::XGfxSurface* pRaw = nullptr;
            if (!Canvas::TerrainViewer::LoadSkyCube(
                    m_pGfxDevice, skyDir, name, &pRaw, m_pLogger.Get()))
            {
                Canvas::LogError(m_pLogger.Get(),
                    "LoadSky: failed to load preset '%s' from '%s'",
                    name, skyDir.string().c_str());
                return false;
            }
            out.Attach(pRaw);
            Gem::ThrowGemError(m_pGfxRenderQueue->FinalizeUploadAsShaderResource(out.Get()));
            return true;
        };

        if (!loadOne("day",   m_pSkyCubeDay))   return false;
        if (!loadOne("dusk",  m_pSkyCubeDusk))  return false;
        if (!loadOne("night", m_pSkyCubeNight)) return false;
        if (!loadOne("stars", m_pSkyCubeStars)) return false;

        // Moon billboard sprite (2D RGBA).  Composite samples it inside
        // the angular disc around MoonDirection (see GfxBackgroundDesc).
        const std::filesystem::path moonPath = skyDir / "moon.png";
        Canvas::Platform::Win32::ImageData moonImg;
        if (!Canvas::Platform::Win32::LoadImageData(
                moonPath.wstring().c_str(),
                Canvas::GfxFormat::R8G8B8A8_UNorm, &moonImg, m_pLogger.Get()))
        {
            Canvas::LogError(m_pLogger.Get(), "LoadSky: failed to load '%s'",
                moonPath.string().c_str());
            return false;
        }
        {
            Canvas::GfxSurfaceDesc desc = Canvas::GfxSurfaceDesc::SurfaceDesc2D(
                Canvas::GfxFormat::R8G8B8A8_UNorm,
                moonImg.Width, moonImg.Height,
                Canvas::SurfaceFlag_ShaderResource);
            Gem::ThrowGemError(m_pGfxDevice->CreateSurface(desc, &m_pMoonTexture));
            Gem::ThrowGemError(m_pGfxDevice->UploadTextureRegion(
                m_pMoonTexture, 0, 0, 0,
                moonImg.Width, moonImg.Height,
                moonImg.Pixels.data(), moonImg.Width * 4));
            Gem::ThrowGemError(m_pGfxRenderQueue->FinalizeUploadAsShaderResource(m_pMoonTexture.Get()));
        }
        return true;
    }

    // Pick the adjacent (A, B) cubemap pair and blend factor for the current
    // time of day, then publish them through the scene background API.  The
    // four anchor presets across the 24-hour cycle are
    //   phase 0.00 = night   0.25 = dusk    0.50 = day    0.75 = dusk
    // each segment crossfading linearly to the next, so dusk is the shared
    // transition preset for both sunrise and sunset.
    void UpdateSkyBackground()
    {
        const float phase   = m_DayNight.GetPhase();
        const float segment = phase * 4.0f;            // [0, 4)
        const int   segIdx  = static_cast<int>(segment);
        const float t       = segment - static_cast<float>(segIdx);

        Canvas::XGfxSurface* pA = nullptr;
        Canvas::XGfxSurface* pB = nullptr;
        switch (segIdx)
        {
            case 0: pA = m_pSkyCubeNight; pB = m_pSkyCubeDusk;  break;  // [00:00, 06:00) night -> dusk
            case 1: pA = m_pSkyCubeDusk;  pB = m_pSkyCubeDay;   break;  // [06:00, 12:00) dusk  -> day
            case 2: pA = m_pSkyCubeDay;   pB = m_pSkyCubeDusk;  break;  // [12:00, 18:00) day   -> dusk
            default: pA = m_pSkyCubeDusk; pB = m_pSkyCubeNight; break;  // [18:00, 24:00) dusk  -> night
        }

        Canvas::GfxBackgroundDesc bg = {};
        bg.pSkyboxCubemapA = pA;
        bg.pSkyboxCubemapB = pB;
        bg.BlendFactor     = t;
        bg.Intensity       = 1.0f;

        // Stars cube: rotated around the world +Y "polar" axis so the
        // entire star sphere sweeps with the diurnal cycle.  Stars cube
        // is authored such that the rest position aligns with world
        // forward (+X) at sunrise; matching the sun/moon rotation keeps
        // their relative positions on the celestial sphere stable.
        //   theta around +Y = pi/2 - 2*pi*phase
        // (same derivation as the prior celestial cube; see
        // CDayNightCycle::Update for the position semantics.)
        const float theta =
            static_cast<float>(Canvas::Math::Pi * 0.5)
            - 2.0f * static_cast<float>(Canvas::Math::Pi) * phase;
        const float halfTheta = 0.5f * theta;
        bg.pStarsCubemap   = m_pSkyCubeStars;
        bg.StarsOrientation = Canvas::Math::FloatQuaternion(
            0.0f, std::sin(halfTheta), 0.0f, std::cos(halfTheta));
        bg.StarsIntensity  = m_DayNight.GetStarsIntensity();

        // Sun: procedural disc driven by the same direction vector the
        // sun directional light uses.  Display colour packs intensity
        // into .a (sun fades to zero across the horizon via sunGate).
        const auto sunPos   = m_DayNight.GetSunPosition();
        const auto sunColor = m_DayNight.GetSunDisplayColor();
        bg.SunDirection     = sunPos;
        bg.SunColor         = sunColor;
        bg.SunAngularRadius = 0.07f;     // ~4 deg, matches the celestial-cube authoring

        // Moon: textured billboard at the anti-sun direction.
        const auto moonPos   = m_DayNight.GetMoonPosition();
        const auto moonColor = m_DayNight.GetMoonDisplayColor();
        bg.pMoonTexture      = m_pMoonTexture;
        bg.MoonDirection     = moonPos;
        bg.MoonColor         = moonColor;
        bg.MoonAngularRadius = 0.05f;    // ~3 deg

        m_pScene->SetBackground(&bg);
    }

public:
    CTerrainApp(
        HINSTANCE hInstance,
        PCSTR     szTitle,
        Gem::TGemPtr<Canvas::XLogger> pLogger,
        int       exitFrameCount,
        bool      logFps,
        Canvas::TerrainViewer::SceneConfig scene,
        float     cycleSeconds)
        : m_Title(szTitle)
        , m_hInstance(hInstance)
        , m_pLogger(pLogger)
        , m_exitFrameCount(exitFrameCount)
        , m_logFps(logFps)
        , m_SceneConfig(std::move(scene))
        , m_CycleSeconds(cycleSeconds)
    {
    }

    bool Initialize(int nCmdShow)
    {
        Canvas::CFunctionSentinel sentinel("CTerrainApp::Initialize", m_pLogger.Get());
        const char* initStep = "startup";

        try
        {
            initStep = "create_window";
            {
                Canvas::Platform::Win32::AppWindowDesc wdesc;
                wdesc.Title     = m_Title.c_str();
                wdesc.hInstance = m_hInstance;
                wdesc.Width     = 1280;
                wdesc.Height    = 768;
                Gem::ThrowGemError(Canvas::Platform::Win32::CreatePlatformWindow(wdesc, &m_pWindow, &m_pInput));
            }
            m_pWindow->Show(nCmdShow);

            initStep = "create_canvas";
            Gem::TGemPtr<Canvas::XCanvas> pCanvas;
            Gem::ThrowGemError(Canvas::CreateCanvas(m_pLogger.Get(), &pCanvas));

            initStep = "load_graphics_plugin";
            Gem::TGemPtr<Canvas::XCanvasPlugin> pGfxPlugin;
            Gem::ThrowGemError(pCanvas->LoadPlugin("CanvasGfx12.dll", &pGfxPlugin));

            initStep = "create_device";
            Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
            Gem::ThrowGemError(pGfxPlugin->CreateCanvasElement(
                pCanvas, Canvas::TypeId::TypeId_GfxDevice,
                "TerrainDevice", Canvas::XGfxDevice::IId,
                reinterpret_cast<void**>(&pDevice)));

            initStep = "create_scene";
            Gem::TGemPtr<Canvas::XScene> pScene;
            Gem::ThrowGemError(pCanvas->CreateScene(pDevice, &pScene, "TerrainScene"));

            initStep = "create_render_queue";
            Gem::TGemPtr<Canvas::XGfxRenderQueue> pRq;
            Gem::ThrowGemError(pDevice->CreateRenderQueue(&pRq));

            initStep = "create_swap_chain";
            Gem::TGemPtr<Canvas::XGfxSwapChain> pSwap;
            Gem::ThrowGemError(pRq->CreateSwapChain(
                m_pWindow->GetHWND(), true, &pSwap,
                Canvas::GfxFormat::R16G16B16A16_Float, 4));

            initStep = "create_camera";
            Gem::TGemPtr<Canvas::XCamera> pCamera;
            Gem::ThrowGemError(pCanvas->CreateCamera(&pCamera, "MainCamera"));
            pCamera->SetNearClip(0.5f);
            pCamera->SetFarClip(4000.0f);
            pCamera->SetFovAngle(static_cast<float>(Canvas::Math::Pi / 3.0));

            Gem::TGemPtr<Canvas::XSceneGraphNode> pCameraNode;
            Gem::ThrowGemError(pCanvas->CreateSceneGraphNode(&pCameraNode, "MainCameraNode"));
            pCameraNode->BindElement(pCamera);
            pScene->GetRootNode()->AddChild(pCameraNode);
            pScene->SetActiveCamera(pCamera);

            // Stash early so LoadTerrain can use them.
            m_pCanvas = pCanvas;
            m_pScene  = pScene;
            m_pGfxPlugin = pGfxPlugin;
            m_pGfxDevice = pDevice;
            m_pGfxRenderQueue = pRq;
            m_pGfxSwapChain   = pSwap;
            m_pCamera = pCamera;

            initStep = "init_day_night";
            m_DayNight.SetCycleSeconds(m_CycleSeconds);
            m_DayNight.Initialize(pCanvas, pScene);

            initStep = "load_terrain";
            if (!LoadTerrain())
                return false;

            initStep = "load_sky";
            if (!LoadSky())
                return false;

            initStep = "load_fonts";
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            const std::filesystem::path fontsDir =
                std::filesystem::path(exePath).parent_path() / "fonts";

            auto LoadFont = [&](const std::filesystem::path& path, PCSTR name) -> Gem::TGemPtr<Canvas::XFont>
            {
                std::ifstream f(path, std::ios::binary | std::ios::ate);
                if (!f.is_open())
                {
                    Canvas::LogError(m_pLogger.Get(), "Cannot open font file: %s", path.string().c_str());
                    Gem::ThrowGemError(Gem::Result::NotFound);
                }
                size_t size = static_cast<size_t>(f.tellg());
                f.seekg(0);
                std::vector<uint8_t> data(size);
                f.read(reinterpret_cast<char*>(data.data()), size);
                Gem::TGemPtr<Canvas::XFont> pF;
                Gem::ThrowGemError(pCanvas->CreateFont(data.data(), data.size(), name, &pF));
                return pF;
            };
            m_pFont     = LoadFont(fontsDir / "Inter-Regular.ttf",         "Inter");
            m_pFontMono = LoadFont(fontsDir / "JetBrainsMono-Regular.ttf", "JetBrainsMono");

            initStep = "create_ui";
            Gem::ThrowGemError(pCanvas->CreateUIGraph(pDevice, &m_pUIGraph));

            Gem::TGemPtr<Canvas::XUIGraphNode> pHudNode;
            Gem::ThrowGemError(m_pUIGraph->CreateNode(nullptr, &pHudNode));
            pHudNode->SetLocalPosition(Canvas::Math::FloatVector2(6.0f, 6.0f));

            Gem::ThrowGemError(pDevice->CreateRectElement(&m_pHudPanel));
            pHudNode->BindElement(m_pHudPanel);
            m_pHudPanel->SetSize(Canvas::Math::FloatVector2(380.0f, 90.0f));
            m_pHudPanel->SetFillColor(Canvas::Math::FloatVector4(0.10f, 0.10f, 0.12f, 0.78f));

            Gem::ThrowGemError(pDevice->CreateTextElement(&m_pTitleText));
            pHudNode->BindElement(m_pTitleText);
            m_pTitleText->SetLocalOffset(Canvas::Math::FloatVector2(8.0f, 6.0f));
            m_pTitleText->SetFont(m_pFont);
            {
                Canvas::TextLayoutConfig cfg;
                cfg.FontSize = 28.0f;
                cfg.Color = Canvas::Math::FloatVector4(1.0f, 1.0f, 1.0f, 1.0f);
                m_pTitleText->SetLayoutConfig(cfg);
            }
            m_pTitleText->SetText("Canvas Terrain Viewer");

            Gem::ThrowGemError(pDevice->CreateTextElement(&m_pStatusText));
            pHudNode->BindElement(m_pStatusText);
            m_pStatusText->SetLocalOffset(Canvas::Math::FloatVector2(8.0f, 42.0f));
            m_pStatusText->SetFont(m_pFontMono);
            {
                Canvas::TextLayoutConfig cfg;
                cfg.FontSize = 16.0f;
                cfg.Color = Canvas::Math::FloatVector4(0.85f, 0.85f, 0.85f, 1.0f);
                m_pStatusText->SetLayoutConfig(cfg);
            }
            m_pStatusText->SetText("loading...");

            return true;
        }
        catch (std::bad_alloc&)
        {
            Canvas::LogError(m_pLogger.Get(), "Initialize failed at '%s' with std::bad_alloc", initStep);
            sentinel.SetResultCode(Gem::Result::OutOfMemory);
            return false;
        }
        catch (Gem::GemError& e)
        {
            Canvas::LogError(m_pLogger.Get(), "Initialize failed at '%s' with Gem::Result=%s",
                initStep, GemResultString(e.Result()));
            sentinel.SetResultCode(e.Result());
            return false;
        }
    }

    int Execute()
    {
        using clock = std::chrono::high_resolution_clock;
        using time_point = std::chrono::time_point<clock>;

        bool running = true;
        float dtime = 0.0f;
        size_t fpsCounter = 0;
        time_point prevTime{};
        time_point fpsTime = clock::now();
        int frameCount = 0;

        constexpr float kMoveSpeedSlow = 32.0f;
        constexpr float kMoveSpeedFast = 320.0f;
        constexpr float kMouseSensitivity = 0.003f;
        constexpr float kPitchLimit = static_cast<float>(Canvas::Math::Pi / 2.0) - 0.01f;

        struct CursorGuard
        {
            Canvas::Platform::Win32::XAppWindow* p;
            ~CursorGuard() { p->SetMouseCaptured(false); }
        } cursorGuard{m_pWindow.Get()};

        m_pWindow->SetMouseCaptured(true);

        for (; running;)
        {
            time_point currTime = clock::now();
            if (prevTime != time_point{})
                dtime = std::chrono::duration<float>(currTime - prevTime).count();
            prevTime = currTime;

            float fpsDt = std::chrono::duration<float>(currTime - fpsTime).count();
            if (fpsDt > 1.0f)
            {
                m_fps = fpsCounter / fpsDt;
                if (m_logFps) Canvas::LogInfo(m_pLogger.Get(), "FPS: %.1f", m_fps);
                fpsTime = currTime;
                fpsCounter = 0;
            }
            ++fpsCounter;
            ++frameCount;

            // Window focus -> mouse capture management.
            const bool hasFocus = m_pWindow->HasFocus();
            if (hasFocus && !m_pWindow->IsMouseCaptured())
                m_pWindow->SetMouseCaptured(true);
            else if (!hasFocus && m_pWindow->IsMouseCaptured())
                m_pWindow->SetMouseCaptured(false);

            // Camera mouse-look.
            float dx = 0.0f, dy = 0.0f;
            bool rotated = false;
            if (m_pWindow->IsMouseCaptured())
            {
                m_pInput->GetMouseDelta(dx, dy);
                if (std::fabs(dx) > 0.0f || std::fabs(dy) > 0.0f)
                {
                    m_CameraYaw   -= dx * kMouseSensitivity;
                    m_CameraPitch -= dy * kMouseSensitivity;
                    m_CameraPitch = std::clamp(m_CameraPitch, -kPitchLimit, kPitchLimit);
                    rotated = true;
                }
            }

            const float cy = std::cos(m_CameraYaw),   sy = std::sin(m_CameraYaw);
            const float cp = std::cos(m_CameraPitch), sp = std::sin(m_CameraPitch);
            Canvas::Math::FloatVector4 forward(cy * cp, sy * cp, sp, 0.0f);
            Canvas::Math::FloatVector4 worldUp(0.0f, 0.0f, 1.0f, 0.0f);
            Canvas::Math::FloatMatrix4x4 rotMat = Canvas::Math::IdentityMatrix<float, 4, 4>();
            rotMat[0] = forward;
            Canvas::Math::ComposePointToBasisVectors(worldUp, forward, rotMat[1], rotMat[2]);
            const auto cameraQuat = Canvas::Math::QuaternionFromRotationMatrix(rotMat);
            Canvas::Math::FloatVector4 right = rotMat[1];

            if (hasFocus)
            {
                Canvas::Math::FloatVector4 moveDir(0.0f, 0.0f, 0.0f, 0.0f);
                if (m_pInput->IsKeyDown('W'))        moveDir = moveDir + forward;
                if (m_pInput->IsKeyDown('S'))        moveDir = moveDir - forward;
                if (m_pInput->IsKeyDown('D'))        moveDir = moveDir - right;
                if (m_pInput->IsKeyDown('A'))        moveDir = moveDir + right;
                if (m_pInput->IsKeyDown(VK_SPACE))   moveDir = moveDir + Canvas::Math::FloatVector4(0,0,1,0);
                if (m_pInput->IsKeyDown(VK_CONTROL)) moveDir = moveDir - Canvas::Math::FloatVector4(0,0,1,0);
                if (m_pInput->IsKeyDown(VK_ESCAPE))  { running = false; break; }

                const bool fast = m_pInput->IsKeyDown(VK_SHIFT);
                const float speed = fast ? kMoveSpeedFast : kMoveSpeedSlow;

                float moveMag = std::sqrt(Canvas::Math::DotProduct(moveDir, moveDir));
                bool moved = false;
                if (moveMag > 0.001f)
                {
                    moveDir = moveDir * (1.0f / moveMag);
                    moved = true;
                }

                auto* pCamNode = m_pCamera->GetAttachedNode();
                if (pCamNode && (rotated || moved))
                {
                    auto pos = pCamNode->GetLocalTranslation();
                    if (moved)
                        pos = pos + moveDir * (speed * dtime);
                    pCamNode->SetLocalTranslation(pos);
                    pCamNode->SetLocalRotation(cameraQuat);
                }

                // Day/night hotkeys (edge-triggered).
                if (m_pInput->IsKeyPressed(VK_OEM_4)) m_DayNight.ScrubMinutes(-15.0f); // [
                if (m_pInput->IsKeyPressed(VK_OEM_6)) m_DayNight.ScrubMinutes( 15.0f); // ]
                if (m_pInput->IsKeyPressed(VK_OEM_5)) m_DayNight.TogglePaused();        // backslash
                if (m_pInput->IsKeyPressed(VK_TAB))
                {
                    const bool newState = !m_pGfxRenderQueue->GetGeometryWireframe();
                    m_pGfxRenderQueue->SetGeometryWireframe(newState);
                    Canvas::LogInfo(m_pLogger.Get(), "Wireframe %s", newState ? "ON" : "OFF");
                }
            }

            m_DayNight.Update(dtime);
            UpdateSkyBackground();

            // Frame submit.
            m_pGfxRenderQueue->BeginFrame(m_pGfxSwapChain);
            m_pScene->Update(dtime);
            m_pScene->SubmitRenderables(m_pGfxRenderQueue);

            // Refresh status HUD.
            char buf[128];
            snprintf(buf, sizeof(buf),
                "FPS %.1f   time %s%s",
                m_fps, m_DayNight.TimeString().c_str(),
                m_DayNight.IsPaused() ? " (paused)" : "");
            if (m_statusString != buf)
            {
                m_statusString = buf;
                m_pStatusText->SetText(buf);
            }

            m_pUIGraph->Update();
            m_pUIGraph->SubmitRenderables(m_pGfxRenderQueue);

            m_pGfxRenderQueue->EndFrame();
            m_pGfxRenderQueue->FlushAndPresent(m_pGfxSwapChain);

            if (m_exitFrameCount > 0 && frameCount >= m_exitFrameCount)
            {
                Canvas::LogInfo(m_pLogger.Get(), "Auto-exiting after %d frames", frameCount);
                running = false;
                continue;
            }

            if (!m_pWindow->PumpMessages())
                running = false;
        }
        return 0;
    }
};

int APIENTRY WinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    HRESULT comInitHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comOwned = SUCCEEDED(comInitHr);
    // RAII guard so every return path (including early-out on help / parse
    // errors) releases COM exactly once.
    struct ComGuard
    {
        bool owned;
        ~ComGuard() { if (owned) CoUninitialize(); }
    } comGuard{comOwned};

    Canvas::CTokenParser tokenParser(lpCmdLine);
    std::vector<const char*> argv;
    argv.push_back("CanvasTerrainViewer");
    for (size_t i = 0; i < tokenParser.GetTokenCount(); ++i)
        argv.push_back(tokenParser[i]);
    int argc = static_cast<int>(argv.size());

    std::string logLevel = "warn";
    std::string logFile;
    bool        logConsole = false;
    bool        logFps     = false;
    std::string scenePath;
    std::string heightmapPath;
    std::string atlasAlbedoPath;
    std::string atlasORMPath;
    int         exitFrameCount = -1;
    // Float sentinels: NaN means "user did not supply this flag" so we know
    // whether to override the corresponding scene field.
    constexpr float kFltUnset = std::numeric_limits<float>::quiet_NaN();
    float       dxy = kFltUnset;
    float       heightScale = kFltUnset;
    float       cycleSeconds = CDayNightCycle::kDefaultCycleSeconds; // viewer pref, no override semantics

    try
    {
        InCommand::CommandParser cmdParser("CanvasTerrainViewer");
        auto& rootCmd = cmdParser.GetAppCommandDecl();
        rootCmd.SetDescription("Canvas Terrain Viewer - heightfield-driven terrain renderer");

        rootCmd.AddOption(InCommand::OptionType::Variable, "exitframecount")
            .SetDescription("Exit application after N frames (useful for automated testing)")
            .BindTo(exitFrameCount);

        rootCmd.AddOption(InCommand::OptionType::Variable, "scene")
            .SetDescription("Path to a JSON scene file (default: assets/CanvasTerrainViewer/scene.json beside the exe)")
            .BindTo(scenePath);

        rootCmd.AddOption(InCommand::OptionType::Variable, "heightmap")
            .SetDescription("Heightfield bitmap path (overrides scene.tiles[0].heightmap)")
            .BindTo(heightmapPath);

        rootCmd.AddOption(InCommand::OptionType::Variable, "atlas-albedo")
            .SetDescription("Material atlas albedo path (overrides scene.material.atlasAlbedo)")
            .BindTo(atlasAlbedoPath);

        rootCmd.AddOption(InCommand::OptionType::Variable, "atlas-orm")
            .SetDescription("Material atlas ORM path (overrides scene.material.atlasORM)")
            .BindTo(atlasORMPath);

        rootCmd.AddOption(InCommand::OptionType::Variable, "dxy")
            .SetDescription("World spacing per heightmap texel, in meters (overrides scene.tiles[0].dxy)")
            .BindTo(dxy);

        rootCmd.AddOption(InCommand::OptionType::Variable, "heightscale")
            .SetDescription("Maximum terrain height in meters (overrides scene.tiles[0].heightScale)")
            .BindTo(heightScale);

        rootCmd.AddOption(InCommand::OptionType::Variable, "cycleseconds")
            .SetDescription("Day/night cycle length in seconds (default 300)")
            .BindTo(cycleSeconds);

        auto& logCmd = rootCmd.AddSubCommand("log");
        logCmd.SetDescription("Configure logging output");
        logCmd.AddOption(InCommand::OptionType::Variable, "level", 'l')
            .SetDescription("Set logging level")
            .SetDomain({"trace","debug","info","warn","error","critical","off"})
            .BindTo(logLevel);
        logCmd.AddOption(InCommand::OptionType::Variable, "file", 'f')
            .SetDescription("Log file path")
            .BindTo(logFile);
        logCmd.AddOption(InCommand::OptionType::Switch, "console", 'c')
            .SetDescription("Spawn a console window with live log output")
            .BindTo(logConsole);
        logCmd.AddOption(InCommand::OptionType::Switch, "fps")
            .SetDescription("Log FPS to log output")
            .BindTo(logFps);

        std::ostringstream helpStream;
        cmdParser.EnableAutoHelp("help", 'h', helpStream);
        cmdParser.ParseArgs(argc, argv.data());
        if (cmdParser.WasAutoHelpRequested())
        {
            AllocConsole();
            SetConsoleTitleA("CanvasTerrainViewer - Help");
            FILE* tmpOut = nullptr; FILE* tmpIn = nullptr;
            freopen_s(&tmpOut, "CONOUT$", "w", stdout);
            freopen_s(&tmpIn,  "CONIN$",  "r", stdin);
            std::cout << helpStream.str() << std::endl;
            std::cout << "Press any key to close..." << std::endl;
            (void)_getch();
            if (tmpOut) fclose(tmpOut);
            if (tmpIn)  fclose(tmpIn);
            FreeConsole();
            return 0;
        }
    }
    catch (const InCommand::SyntaxException& e)
    {
        std::ostringstream oss;
        oss << "Command line error: " << e.GetMessage();
        if (!e.GetToken().empty()) oss << " (token: '" << e.GetToken() << "')";
        MessageBoxA(nullptr, oss.str().c_str(), "CanvasTerrainViewer", MB_OK | MB_ICONERROR);
        return -1;
    }
    catch (const InCommand::ApiException& e)
    {
        std::ostringstream oss;
        oss << "Internal error: " << e.GetMessage();
        MessageBoxA(nullptr, oss.str().c_str(), "CanvasTerrainViewer", MB_OK | MB_ICONERROR);
        return -1;
    }

    QLog::Level qlogLevel = QLog::Level::Warn;
    if      (logLevel == "trace")    qlogLevel = QLog::Level::Trace;
    else if (logLevel == "debug")    qlogLevel = QLog::Level::Debug;
    else if (logLevel == "info")     qlogLevel = QLog::Level::Info;
    else if (logLevel == "warn")     qlogLevel = QLog::Level::Warn;
    else if (logLevel == "error")    qlogLevel = QLog::Level::Error;
    else if (logLevel == "critical") qlogLevel = QLog::Level::Critical;
    else if (logLevel == "off")      qlogLevel = QLog::Level::Off;

    wchar_t exePathBuf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
    std::filesystem::path logFilePath;
    if (logFile.empty())
    {
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &tt);
        char timeBuf[32];
        std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d%H%M%S", &tm);
        logFilePath = std::filesystem::path(exePathBuf).parent_path() /
            (std::string("CanvasTerrainViewer_") + timeBuf + ".log");
    }
    else
        logFilePath = logFile;

    std::unique_ptr<CConsole> pConsole;
    if (logConsole) pConsole = std::make_unique<CConsole>();

    CLogSink logSink(logFilePath, logConsole);
    auto qlogLogger = std::make_unique<QLog::Logger>(logSink, qlogLevel);

    Gem::TGemPtr<Canvas::QLogAdapter> pAdapter;
    Gem::ThrowGemError(Gem::TGenericImpl<Canvas::QLogAdapter>::Create(
        &pAdapter, qlogLogger.release()));
    Gem::TGemPtr<Canvas::XLogger> pLogger;
    pAdapter->QueryInterface(&pLogger);

    Canvas::LogInfo(pLogger.Get(), "Log file: %s", logFilePath.string().c_str());
    Canvas::LogInfo(pLogger.Get(),
        "Startup: --scene='%s' --heightmap='%s' --atlas-albedo='%s' --atlas-orm='%s' "
        "--dxy=%g --heightscale=%g --cycleseconds=%.1f log='%s'",
        scenePath.c_str(), heightmapPath.c_str(), atlasAlbedoPath.c_str(), atlasORMPath.c_str(),
        static_cast<double>(dxy), static_cast<double>(heightScale),
        cycleSeconds, logLevel.c_str());

    auto pathFromUtf8 = [](const std::string& s) {
        return s.empty() ? std::filesystem::path{} : std::filesystem::u8path(s);
    };

    // -------------------------------------------------------------------------
    // Scene: load JSON (or fall back to built-in defaults), then apply CLI
    // overrides on top.
    // -------------------------------------------------------------------------
    const std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();

    Canvas::TerrainViewer::SceneConfig scene;
    Canvas::TerrainViewer::ApplyDefaults(scene);
    // Resolve the built-in default paths against the exe directory, which is
    // where the staged sample assets live.
    {
        auto resolveExe = [&](std::filesystem::path& p) {
            if (!p.empty() && p.is_relative())
                p = std::filesystem::weakly_canonical(exeDir / p);
        };
        for (auto& t : scene.Tiles) resolveExe(t.Heightmap);
        resolveExe(scene.Material.AtlasAlbedo);
        resolveExe(scene.Material.AtlasORM);
    }

    std::filesystem::path resolvedScenePath;
    if (!scenePath.empty())
        resolvedScenePath = pathFromUtf8(scenePath);
    else if (std::filesystem::exists(exeDir / "assets" / "CanvasTerrainViewer" / "scene.json"))
        resolvedScenePath = exeDir / "assets" / "CanvasTerrainViewer" / "scene.json";

    if (!resolvedScenePath.empty())
    {
        if (!Canvas::TerrainViewer::LoadSceneConfig(resolvedScenePath, &scene, pLogger.Get()))
        {
            Canvas::LogError(pLogger.Get(), "Failed to load scene '%s'; aborting",
                resolvedScenePath.string().c_str());
            return FALSE;
        }
    }
    else
    {
        Canvas::LogInfo(pLogger.Get(), "No --scene supplied and no assets/CanvasTerrainViewer/scene.json found; using built-in defaults");
    }

    // Apply CLI overrides on top of the loaded/default scene.
    if (scene.Tiles.empty()) scene.Tiles.emplace_back();
    Canvas::TerrainViewer::SceneTile& tile0 = scene.Tiles.front();
    if (!heightmapPath.empty())   tile0.Heightmap = pathFromUtf8(heightmapPath);
    if (std::isfinite(dxy))       tile0.Dxy = dxy;
    if (std::isfinite(heightScale)) tile0.HeightScale = heightScale;
    if (!atlasAlbedoPath.empty()) scene.Material.AtlasAlbedo = pathFromUtf8(atlasAlbedoPath);
    if (!atlasORMPath.empty())    scene.Material.AtlasORM    = pathFromUtf8(atlasORMPath);

    auto pApp = std::make_unique<CTerrainApp>(
        hInstance, "CanvasTerrainViewer", pLogger,
        exitFrameCount, logFps,
        std::move(scene), cycleSeconds);

    if (!pApp->Initialize(nCmdShow))
    {
        Canvas::LogError(pLogger.Get(), "Application initialization failed; exiting");
        return FALSE;
    }

    int rc = pApp->Execute();
    pApp.reset();
    return rc;
}
