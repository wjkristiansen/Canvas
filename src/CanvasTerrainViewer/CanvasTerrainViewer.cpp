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
#include "ImageLoader.h"
#include "SceneConfig.h"
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
// Window subclass providing raw-mouse-input deltas for the FPS camera. Identical
// to the CanvasModelViewer version; see that file for the design notes.
class CViewerWindow : public ThinWin::CWindow
{
public:
    using CWindow::CWindow;

    bool IsMouseCaptured() const { return m_Captured; }

    void SetMouseCaptured(bool captured)
    {
        if (captured && !m_Captured)
        {
            ::SetCapture(m_hWnd);
            ::SetCursor(NULL);

            RECT rc;
            ::GetClientRect(m_hWnd, &rc);
            ::MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<POINT*>(&rc), 2);
            ::ClipCursor(&rc);

            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01;
            rid.usUsage     = 0x02;
            rid.dwFlags     = 0;
            rid.hwndTarget  = m_hWnd;
            ::RegisterRawInputDevices(&rid, 1, sizeof(rid));

            m_Captured = true;
            m_HasLastAbsPos = false;
            m_MouseDX = m_MouseDY = 0.0f;
        }
        else if (!captured && m_Captured)
        {
            // Flip the flag first so a re-entrant WM_CAPTURECHANGED from
            // ReleaseCapture() below is a no-op.
            m_Captured = false;

            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01;
            rid.usUsage     = 0x02;
            rid.dwFlags     = RIDEV_REMOVE;
            rid.hwndTarget  = nullptr;
            ::RegisterRawInputDevices(&rid, 1, sizeof(rid));

            ::ClipCursor(nullptr);
            ::ReleaseCapture();
            ::SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
    }

    void ConsumeMouseDelta(float& dx, float& dy)
    {
        dx = m_MouseDX;
        dy = m_MouseDY;
        m_MouseDX = m_MouseDY = 0.0f;
    }

    LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override
    {
        switch (uMsg)
        {
        case WM_INPUT:
            if (m_Captured)
            {
                UINT dwSize = sizeof(RAWINPUT);
                alignas(RAWINPUT) BYTE buffer[sizeof(RAWINPUT)];
                if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),
                        RID_INPUT, buffer, &dwSize, sizeof(RAWINPUTHEADER)) != UINT(-1))
                {
                    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer);
                    if (raw->header.dwType == RIM_TYPEMOUSE)
                    {
                        const RAWMOUSE& mouse = raw->data.mouse;
                        if (mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
                        {
                            float absX, absY;
                            if (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP)
                            {
                                int vdW = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
                                int vdH = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
                                absX = (mouse.lLastX / 65535.0f) * vdW;
                                absY = (mouse.lLastY / 65535.0f) * vdH;
                            }
                            else
                            {
                                absX = (mouse.lLastX / 65535.0f) * ::GetSystemMetrics(SM_CXSCREEN);
                                absY = (mouse.lLastY / 65535.0f) * ::GetSystemMetrics(SM_CYSCREEN);
                            }
                            if (m_HasLastAbsPos)
                            {
                                m_MouseDX += absX - m_LastAbsX;
                                m_MouseDY += absY - m_LastAbsY;
                            }
                            m_LastAbsX = absX;
                            m_LastAbsY = absY;
                            m_HasLastAbsPos = true;
                        }
                        else
                        {
                            m_MouseDX += static_cast<float>(mouse.lLastX);
                            m_MouseDY += static_cast<float>(mouse.lLastY);
                        }
                    }
                }
                return 0;
            }
            break;

        case WM_MOUSEMOVE:
            if (m_Captured)
            {
                ::SetCursor(NULL);
                return 0;
            }
            break;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                ::SetCursor(m_Captured ? NULL : LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_CAPTURECHANGED:
            // Capture lost externally (Alt+Tab, focus change, ClipCursor reset
            // by another app, etc.). Run the full release path so the raw-input
            // device registration and cursor clipping are cleaned up too.
            if (reinterpret_cast<HWND>(lParam) != m_hWnd)
                SetMouseCaptured(false);
            return 0;
        }

        return CWindow::WindowProc(uMsg, wParam, lParam);
    }

private:
    float m_MouseDX = 0.0f;
    float m_MouseDY = 0.0f;
    float m_LastAbsX = 0.0f;
    float m_LastAbsY = 0.0f;
    bool  m_HasLastAbsPos = false;
    bool  m_Captured = false;
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

        m_pSun->SetFlags(Canvas::LightFlags::Enabled);
        m_pMoon->SetFlags(Canvas::LightFlags::Enabled);
        m_pAmbient->SetFlags(Canvas::LightFlags::Enabled);

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
        // lighting on the first frame.
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
        // Sun's apparent position in the sky: rises in +X, peaks at +Z, sets
        // in -X. The deferred shader treats the directional light's basis-row-0
        // forward as the *direction photons travel* (it negates it to compute
        // L), so we point the light *away* from the sun's position. Moon is
        // the polar opposite.
        const float ct = std::cos(theta);
        const float st = std::sin(theta);

        const Canvas::Math::FloatVector4 sunPos (ct, 0.0f, st, 0.0f);
        const Canvas::Math::FloatVector4 sunDir  = -sunPos;  // photons travel away from sun
        const Canvas::Math::FloatVector4 moonDir =  sunPos;  // polar opposite
        const Canvas::Math::FloatVector4 worldUp(0.0f, 0.0f, 1.0f, 0.0f);

        OrientLightNode(m_pSunNode,  sunDir,  worldUp);
        OrientLightNode(m_pMoonNode, moonDir, worldUp);

        // Intensity fades smoothly across the horizon. smoothstep(-0.1, 0.2, sin)
        // gives a gentle dawn/dusk crossfade where both lights contribute.
        const float sunGate  = SmoothStep(-0.1f, 0.2f,  st);
        const float moonGate = SmoothStep(-0.1f, 0.2f, -st);

        // Warm sun: yellowish-white. Color tints toward orange near horizon.
        const float horizonness = 1.0f - std::min(1.0f, std::abs(st) * 2.5f);
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
    float m_CycleSeconds = kDefaultCycleSeconds;
    float m_TimeOfDay    = 0.0f;
    bool  m_Paused       = false;
};

} // anonymous namespace

//-------------------------------------------------------------------------------------------------
class CTerrainApp
{
    std::string m_Title;
    HINSTANCE   m_hInstance;
    Gem::TGemPtr<Canvas::XLogger>           m_pLogger;
    std::unique_ptr<CViewerWindow>          m_pWindow;
    Gem::TGemPtr<Canvas::XCanvas>           m_pCanvas;
    Gem::TGemPtr<Canvas::XScene>       m_pScene;
    Gem::TGemPtr<Canvas::XCamera>           m_pCamera;
    Gem::TGemPtr<Canvas::XCanvasPlugin>     m_pGfxPlugin;
    Gem::TGemPtr<Canvas::XGfxDevice>        m_pGfxDevice;
    Gem::TGemPtr<Canvas::XGfxSwapChain>     m_pGfxSwapChain;
    Gem::TGemPtr<Canvas::XGfxRenderQueue>   m_pGfxRenderQueue;
    Gem::TGemPtr<Canvas::XGfxSurface>       m_pHeightmapSurface;
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

        Canvas::TerrainViewer::ImageRGBA8 albedoAtlas, ormAtlas;
        if (!Canvas::TerrainViewer::LoadImageRGBA8(mat.AtlasAlbedo.wstring().c_str(),
                &albedoAtlas, m_pLogger.Get()))
        {
            Canvas::LogError(m_pLogger.Get(), "Failed to load atlas albedo '%s'",
                mat.AtlasAlbedo.string().c_str());
            return false;
        }
        if (!Canvas::TerrainViewer::LoadImageRGBA8(mat.AtlasORM.wstring().c_str(),
                &ormAtlas, m_pLogger.Get()))
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
        disp.pHeightmap        = m_pHeightmapSurface;
        disp.HeightScale       = field.Desc.HeightScale;
        disp.HeightBias        = field.Desc.HeightBias;
        disp.TileSizeWorldX    = field.WorldWidth();
        disp.TileSizeWorldY    = field.WorldHeight();
        disp.MinTessFactor     = 2.0f;
        disp.MaxTessFactor     = 32.0f;
        disp.DistanceLodScale  = 10.0f;
        disp.CurvatureLodScale = 0.5f;
        m_pTerrainMaterial->SetDisplacement(&disp);

        // Procedural patch-grid mesh: [0,1]^2 unit-square grid of 64x64
        // quad patches. The scene-graph node's local transform scales
        // this to (WorldSizeX, WorldSizeY, 1) and translates to the
        // tile's world origin.
        char meshName[64];
        snprintf(meshName, sizeof(meshName), "TerrainPatchMesh_64x64");
        Gem::ThrowGemError(m_pGfxDevice->CreateProceduralPatchGrid(
            64, m_pTerrainMaterial, &m_pTerrainPatchMesh, meshName));

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
            auto pWindow = std::make_unique<CViewerWindow>(
                m_Title.c_str(), m_hInstance, WS_OVERLAPPEDWINDOW);

            pWindow->ShowWindow(nCmdShow);
            pWindow->UpdateWindow();

            initStep = "create_canvas";
            Gem::TGemPtr<Canvas::XCanvas> pCanvas;
            Gem::ThrowGemError(Canvas::CreateCanvas(m_pLogger.Get(), &pCanvas));

            // Default windowed size.
            RECT rcWnd; GetWindowRect(pWindow->m_hWnd, &rcWnd);
            SetWindowPos(pWindow->m_hWnd, NULL, rcWnd.left, rcWnd.top,
                1280, 768, SWP_NOZORDER);

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
                pWindow->m_hWnd, true, &pSwap,
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

            m_pWindow = std::move(pWindow);
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
            CViewerWindow* p;
            ~CursorGuard() { if (p) p->SetMouseCaptured(false); }
        } cursorGuard{m_pWindow.get()};

        m_pWindow->SetMouseCaptured(true);

        // Edge-detect helpers for hotkeys.
        bool prevPause = false, prevScrubBack = false, prevScrubFwd = false, prevWire = false;

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
            HWND foreground = GetForegroundWindow();
            HWND foregroundRoot = foreground ? GetAncestor(foreground, GA_ROOT) : nullptr;
            const bool hasFocus = (foreground == m_pWindow->m_hWnd) ||
                                  (foregroundRoot == m_pWindow->m_hWnd) ||
                                  (foreground && IsChild(m_pWindow->m_hWnd, foreground));
            if (hasFocus && !m_pWindow->IsMouseCaptured())
                m_pWindow->SetMouseCaptured(true);
            else if (!hasFocus && m_pWindow->IsMouseCaptured())
                m_pWindow->SetMouseCaptured(false);

            // Camera mouse-look.
            float dx = 0.0f, dy = 0.0f;
            bool rotated = false;
            if (m_pWindow->IsMouseCaptured())
            {
                m_pWindow->ConsumeMouseDelta(dx, dy);
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
                if (GetAsyncKeyState('W') & 0x8000) moveDir = moveDir + forward;
                if (GetAsyncKeyState('S') & 0x8000) moveDir = moveDir - forward;
                if (GetAsyncKeyState('D') & 0x8000) moveDir = moveDir - right;
                if (GetAsyncKeyState('A') & 0x8000) moveDir = moveDir + right;
                if (GetAsyncKeyState(VK_SPACE)   & 0x8000) moveDir = moveDir + Canvas::Math::FloatVector4(0,0,1,0);
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) moveDir = moveDir - Canvas::Math::FloatVector4(0,0,1,0);
                if (GetAsyncKeyState(VK_ESCAPE)  & 0x8000) { running = false; break; }

                const bool fast = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
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
                const bool kBack = (GetAsyncKeyState(VK_OEM_4) & 0x8000) != 0; // [
                const bool kFwd  = (GetAsyncKeyState(VK_OEM_6) & 0x8000) != 0; // ]
                const bool kPause= (GetAsyncKeyState(VK_OEM_5) & 0x8000) != 0; // backslash
                const bool kWire = (GetAsyncKeyState(VK_TAB)   & 0x8000) != 0;
                if (kBack && !prevScrubBack) m_DayNight.ScrubMinutes(-15.0f);
                if (kFwd  && !prevScrubFwd ) m_DayNight.ScrubMinutes( 15.0f);
                if (kPause&& !prevPause    ) m_DayNight.TogglePaused();
                if (kWire && !prevWire     )
                {
                    const bool newState = !m_pGfxRenderQueue->GetGeometryWireframe();
                    m_pGfxRenderQueue->SetGeometryWireframe(newState);
                    Canvas::LogInfo(m_pLogger.Get(), "Wireframe %s", newState ? "ON" : "OFF");
                }
                prevScrubBack = kBack; prevScrubFwd = kFwd; prevPause = kPause; prevWire = kWire;
            }

            m_DayNight.Update(dtime);

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

            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                if (msg.message == WM_QUIT)
                    running = false;
            }
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

    ThinWin::CWindow::RegisterWindowClass(hInstance);

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
