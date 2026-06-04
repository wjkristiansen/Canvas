// CanvasLightGarden.cpp
//
// Procedural, deterministic many-light sample for the Canvas engine.
//
// Builds an N x N grid of open-topped "courtyard" cells from a small
// palette of mesh-instanced primitives (floor plane, wall cube,
// column, prop cube).  Each cell hosts one point light and (~30% of
// cells) one spot light, both seeded from a fixed PRNG so the layout
// is bit-identical across runs.
//
// The sample exists as a stress test for light culling, view-space
// tiling, and shadow culling: heavy on light count and instance count
// but trivial on art-asset dependencies.  Directional sun gets
// shadows; point and spot lights run shadow-free.
//
// Controls:
//   WASD / Space-Ctrl : move
//   Mouse             : look
//   Tab               : toggle camera capture
//   Alt+Enter         : toggle borderless fullscreen
//   Esc               : exit

#include "pch.h"
#include "QLogAdapter.h"
#include "CanvasCore.h"
#include "CanvasGfx.h"
#include "TokenParser.h"
#include "InCommand.h"

#include <conio.h>

// D3D12 Agility SDK version exports - same as the other Canvas apps.
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 616; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

namespace
{

using Canvas::Math::FloatVector2;
using Canvas::Math::FloatVector4;
using Canvas::Math::FloatMatrix4x4;
using Canvas::Math::FloatQuaternion;

// ------------------------------------------------------------------
// Logger sink that writes to OutputDebugString + an optional log file
// and (optionally) a dedicated console window.  Stripped-down
// counterpart of the sink in CanvasModelViewer.
// ------------------------------------------------------------------
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
        oss << QLog::FormatTimestamp(message)
            << QLog::ToString(message.level)
            << ": " << message.text << '\n';
        const std::string formatted = oss.str();

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

class CConsole
{
    FILE* m_stdout = nullptr;
    FILE* m_stderr = nullptr;

public:
    CConsole()
    {
        AllocConsole();
        SetConsoleTitleA("CanvasLightGarden - Log");
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

// ------------------------------------------------------------------
// Procedural cube (unit cube centered at origin, axis-aligned).  Each
// face is a quad emitted via EmitQuad such that du x dv == +normal,
// keeping the CCW winding outward-facing for the default D3D12
// CULL_BACK rasterizer state.  Positions in [-0.5, +0.5]^3; normals
// are face-aligned (each face owns 4 verts, 2 tris = 6 verts).
// ------------------------------------------------------------------
struct MeshCpu
{
    std::vector<FloatVector4> Positions;  // W = 1
    std::vector<FloatVector4> Normals;    // W = 0
};

void EmitQuad(MeshCpu& mesh,
              const FloatVector4& origin,
              const FloatVector4& du,
              const FloatVector4& dv,
              const FloatVector4& normal)
{
    const FloatVector4 v0 = origin;
    const FloatVector4 v1 = origin + du;
    const FloatVector4 v2 = origin + du + dv;
    const FloatVector4 v3 = origin + dv;

    auto pos = [](const FloatVector4& v) {
        return FloatVector4(v.X, v.Y, v.Z, 1.0f);
    };

    mesh.Positions.push_back(pos(v0));
    mesh.Positions.push_back(pos(v1));
    mesh.Positions.push_back(pos(v2));
    mesh.Positions.push_back(pos(v0));
    mesh.Positions.push_back(pos(v2));
    mesh.Positions.push_back(pos(v3));

    for (int i = 0; i < 6; ++i)
        mesh.Normals.push_back(normal);
}

MeshCpu BuildUnitCube()
{
    MeshCpu m;
    m.Positions.reserve(36);
    m.Normals  .reserve(36);

    // +X face
    EmitQuad(m, { 0.5f, -0.5f, -0.5f, 0.0f }, { 0,  1, 0, 0 }, { 0, 0, 1, 0 }, {  1, 0, 0, 0 });
    // -X face
    EmitQuad(m, {-0.5f,  0.5f, -0.5f, 0.0f }, { 0, -1, 0, 0 }, { 0, 0, 1, 0 }, { -1, 0, 0, 0 });
    // +Y face
    EmitQuad(m, { 0.5f,  0.5f, -0.5f, 0.0f }, {-1,  0, 0, 0 }, { 0, 0, 1, 0 }, { 0,  1, 0, 0 });
    // -Y face
    EmitQuad(m, {-0.5f, -0.5f, -0.5f, 0.0f }, { 1,  0, 0, 0 }, { 0, 0, 1, 0 }, { 0, -1, 0, 0 });
    // +Z face
    EmitQuad(m, {-0.5f, -0.5f,  0.5f, 0.0f }, { 1,  0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0,  1, 0 });
    // -Z face
    EmitQuad(m, {-0.5f,  0.5f, -0.5f, 0.0f }, { 1,  0, 0, 0 }, { 0,-1, 0, 0 }, { 0, 0, -1, 0 });

    return m;
}

// Single quad in the XY plane, normal +Z, spanning [-0.5, +0.5] x [-0.5, +0.5].
MeshCpu BuildUnitPlane()
{
    MeshCpu m;
    m.Positions.reserve(6);
    m.Normals  .reserve(6);
    EmitQuad(m, {-0.5f, -0.5f, 0.0f, 0.0f }, { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 });
    return m;
}

Gem::Result CreateMeshFromCpu(Canvas::XGfxDevice* pDevice,
                              const MeshCpu& cpu,
                              const char* name,
                              Canvas::XGfxMeshData** ppOut)
{
    Canvas::MeshDataGroupDesc group = {};
    group.VertexCount = static_cast<uint32_t>(cpu.Positions.size());
    group.pPositions  = cpu.Positions.data();
    group.pNormals    = cpu.Normals.data();

    Canvas::MeshDataDesc desc = {};
    desc.pGroups    = &group;
    desc.GroupCount = 1;
    desc.pName      = name;
    desc.Topology   = Canvas::GfxPrimitiveTopology::TriangleList;

    return pDevice->CreateMeshData(desc, ppOut);
}

// Build the row-vector basis matrix that orients an object's local +X
// (forward) along `forward`, and return the corresponding quaternion.
FloatQuaternion QuaternionLookingAlong(const FloatVector4& forward,
                                       const FloatVector4& worldUp)
{
    FloatMatrix4x4 m = Canvas::Math::IdentityMatrix<float, 4, 4>();
    m[0] = forward.Normalize();
    Canvas::Math::ComposePointToBasisVectors(worldUp, m[0], m[1], m[2]);
    return Canvas::Math::QuaternionFromRotationMatrix(m);
}

// ------------------------------------------------------------------
// Procedural scene builder.  All randomness flows from a single
// std::mt19937 seeded by the caller; the layout is therefore
// bit-identical across runs for a given (gridSize, seed) pair.
// ------------------------------------------------------------------
struct GardenStats
{
    uint32_t PointLights    = 0;
    uint32_t SpotLights     = 0;
    uint32_t MeshInstances  = 0;
};

struct GardenConfig
{
    uint32_t GridSize     = 8;       // cells per side
    float    CellMeters   = 10.0f;   // size of each cell
    float    WallHeight   = 2.0f;
    uint32_t Seed         = 0xC0FFEEu;
    float    SpotChance   = 0.30f;   // P(spot light per cell)
    uint32_t MaxPropsCell = 3;
    bool     ShadowsAll   = false;   // request shadows on every light (no-op for point/spot)
};

class GardenBuilder
{
    Canvas::XCanvas*     m_pCanvas    = nullptr;
    Canvas::XGfxDevice*  m_pDevice    = nullptr;
    Canvas::XScene*      m_pScene     = nullptr;
    Canvas::XLogger*     m_pLogger    = nullptr;

    Gem::TGemPtr<Canvas::XGfxMeshData> m_pFloorMesh;
    Gem::TGemPtr<Canvas::XGfxMeshData> m_pCubeMesh;

    // Keep all instances alive for the lifetime of the scene.
    std::vector<Gem::TGemPtr<Canvas::XMeshInstance>> m_Instances;
    std::vector<Gem::TGemPtr<Canvas::XSceneGraphNode>> m_Nodes;
    std::vector<Gem::TGemPtr<Canvas::XLight>> m_Lights;

public:
    GardenStats Stats;

    GardenBuilder(Canvas::XCanvas* pCanvas,
                  Canvas::XGfxDevice* pDevice,
                  Canvas::XScene* pScene,
                  Canvas::XLogger* pLogger)
        : m_pCanvas(pCanvas), m_pDevice(pDevice), m_pScene(pScene), m_pLogger(pLogger)
    {}

    void Build(const GardenConfig& cfg)
    {
        // ---- Shared meshes (one of each, instanced many times) -----
        Gem::ThrowGemError(CreateMeshFromCpu(m_pDevice, BuildUnitPlane(), "FloorPlane", &m_pFloorMesh));
        Gem::ThrowGemError(CreateMeshFromCpu(m_pDevice, BuildUnitCube(),  "UnitCube",   &m_pCubeMesh));

        const float W = cfg.CellMeters * cfg.GridSize; // world footprint
        const float halfW = 0.5f * W;

        // ---- Floor: one big scaled plane instance ------------------
        AddInstance(m_pFloorMesh.Get(),
                    FloatVector4(0, 0, 0, 1),
                    FloatVector4(W, W, 1, 1),
                    FloatQuaternion{},
                    "Floor");

        // ---- Baseline lights: ambient + directional sun ------------
        AddAmbient(FloatVector4(0.08f, 0.09f, 0.12f, 1.0f), 0.4f);
        AddDirectionalSun(FloatVector4(-0.4f, -0.3f, -1.0f, 0.0f),
                          FloatVector4(1.00f, 0.95f, 0.85f, 1.0f),
                          0.8f);

        // ---- Per-cell content --------------------------------------
        std::mt19937 rng(cfg.Seed);
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        std::uniform_real_distribution<float> propScale(0.5f, 1.5f);
        std::uniform_real_distribution<float> propOffset(-2.5f, 2.5f);
        std::uniform_real_distribution<float> rotAngle(0.0f, 2.0f * 3.14159265f);
        std::uniform_int_distribution<int>    propCount(1, static_cast<int>(cfg.MaxPropsCell));

        for (uint32_t gy = 0; gy < cfg.GridSize; ++gy)
        for (uint32_t gx = 0; gx < cfg.GridSize; ++gx)
        {
            const float cx = -halfW + (gx + 0.5f) * cfg.CellMeters;
            const float cy = -halfW + (gy + 0.5f) * cfg.CellMeters;

            // Cell perimeter walls.  Each cell owns its full perimeter
            // (duplicates between cells are accepted; this is the
            // stress test we wanted).  Walls are scaled cube
            // instances 0.3m thick.
            const float h = cfg.WallHeight;
            const float t = 0.3f;
            const float s = cfg.CellMeters;
            const float halfS = 0.5f * s;
            // +X wall, -X wall, +Y wall, -Y wall
            AddInstance(m_pCubeMesh.Get(), { cx + halfS, cy, h * 0.5f, 1 }, {  t,  s,  h, 1 },
                        FloatQuaternion{}, "Wall+X");
            AddInstance(m_pCubeMesh.Get(), { cx - halfS, cy, h * 0.5f, 1 }, {  t,  s,  h, 1 },
                        FloatQuaternion{}, "Wall-X");
            AddInstance(m_pCubeMesh.Get(), { cx, cy + halfS, h * 0.5f, 1 }, {  s,  t,  h, 1 },
                        FloatQuaternion{}, "Wall+Y");
            AddInstance(m_pCubeMesh.Get(), { cx, cy - halfS, h * 0.5f, 1 }, {  s,  t,  h, 1 },
                        FloatQuaternion{}, "Wall-Y");

            // Props inside the cell.
            const int props = propCount(rng);
            for (int i = 0; i < props; ++i)
            {
                const float sx = propScale(rng);
                const float sy = propScale(rng);
                const float sz = propScale(rng) * 1.5f;
                const float px = cx + propOffset(rng);
                const float py = cy + propOffset(rng);
                const float yaw = rotAngle(rng);
                const FloatQuaternion q = Canvas::Math::QuaternionFromRotationMatrix(
                    YawRotation(yaw));
                AddInstance(m_pCubeMesh.Get(),
                            FloatVector4(px, py, sz * 0.5f, 1),
                            FloatVector4(sx, sy, sz, 1),
                            q, "Prop");
            }

            // Per-cell point light (warm/cool palette).
            const float warmness = u01(rng);
            const FloatVector4 color = Lerp(
                FloatVector4(0.45f, 0.65f, 1.00f, 1.0f),   // cool
                FloatVector4(1.00f, 0.70f, 0.35f, 1.0f),   // warm
                warmness);
            const float py = cy + propOffset(rng) * 0.3f;
            const float px = cx + propOffset(rng) * 0.3f;
            AddPointLight(FloatVector4(px, py, h * 0.85f, 1),
                          color, 8.0f, cfg.CellMeters * 0.9f,
                          cfg.ShadowsAll);

            // ~30% of cells: a spot light angled at a prop.
            if (u01(rng) < cfg.SpotChance)
            {
                const float yaw = rotAngle(rng);
                const float pitch = -0.6f - 0.5f * u01(rng); // angled downward
                FloatVector4 dir(
                    std::cos(yaw) * std::cos(pitch),
                    std::sin(yaw) * std::cos(pitch),
                    std::sin(pitch), 0.0f);
                // Spot palette mirrors the point palette but stays
                // closer to white -- spots read as accent fixtures,
                // not the cell's primary ambience.
                const float spotWarmness = u01(rng);
                const FloatVector4 spotColor = Lerp(
                    FloatVector4(0.70f, 0.85f, 1.00f, 1.0f),   // cool
                    FloatVector4(1.00f, 0.85f, 0.55f, 1.0f),   // warm
                    spotWarmness);
                AddSpotLight(FloatVector4(cx, cy, h * 1.1f, 1),
                             dir.Normalize(),
                             spotColor,
                             // Authored as cone-flux: the shader divides by the
                             // outer-cone solid angle, so 0.5 here lands at a
                             // peak radiance on the same order as the point
                             // lights above (which divide by 4*pi instead).
                             0.5f,
                             cfg.CellMeters * 1.2f,
                             /*innerDeg*/ 18.0f,
                             /*outerDeg*/ 28.0f,
                             cfg.ShadowsAll);
            }
        }

        Canvas::LogInfo(m_pLogger,
            "LightGarden built: grid=%ux%u cell=%.1fm instances=%u pointLights=%u spotLights=%u",
            cfg.GridSize, cfg.GridSize, cfg.CellMeters,
            Stats.MeshInstances, Stats.PointLights, Stats.SpotLights);
    }

private:
    static FloatVector4 Lerp(const FloatVector4& a, const FloatVector4& b, float t)
    {
        return a * (1.0f - t) + b * t;
    }

    static FloatMatrix4x4 YawRotation(float yaw)
    {
        FloatMatrix4x4 m = Canvas::Math::IdentityMatrix<float, 4, 4>();
        const float c = std::cos(yaw), s = std::sin(yaw);
        m[0] = FloatVector4( c,  s, 0, 0);
        m[1] = FloatVector4(-s,  c, 0, 0);
        m[2] = FloatVector4( 0,  0, 1, 0);
        return m;
    }

    void AddInstance(Canvas::XGfxMeshData* pMesh,
                     const FloatVector4& translation,
                     const FloatVector4& scale,
                     const FloatQuaternion& rotation,
                     const char* nameHint)
    {
        Gem::TGemPtr<Canvas::XMeshInstance> pInst;
        Gem::ThrowGemError(m_pCanvas->CreateMeshInstance(&pInst, nameHint));
        pInst->SetMeshData(pMesh);

        Gem::TGemPtr<Canvas::XSceneGraphNode> pNode;
        Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&pNode, nameHint));
        pNode->SetLocalTranslation(translation);
        pNode->SetLocalScale(scale);
        pNode->SetLocalRotation(rotation);
        pNode->BindElement(pInst);
        m_pScene->GetRootNode()->AddChild(pNode);

        m_Instances.emplace_back(std::move(pInst));
        m_Nodes    .emplace_back(std::move(pNode));
        ++Stats.MeshInstances;
    }

    void AddAmbient(const FloatVector4& color, float intensity)
    {
        Gem::TGemPtr<Canvas::XLight> pLight;
        Gem::ThrowGemError(m_pCanvas->CreateLight(Canvas::LightType::Ambient, &pLight, "Ambient"));
        pLight->SetColor(color);
        pLight->SetIntensity(intensity);
        pLight->SetFlags(Canvas::LightFlags::Enabled);
        AttachLight(pLight, FloatVector4(0, 0, 0, 1),
                    FloatQuaternion{}, "AmbientNode");
    }

    void AddDirectionalSun(const FloatVector4& photonDir,
                           const FloatVector4& color, float intensity)
    {
        Gem::TGemPtr<Canvas::XLight> pLight;
        Gem::ThrowGemError(m_pCanvas->CreateLight(Canvas::LightType::Directional, &pLight, "Sun"));
        pLight->SetColor(color);
        pLight->SetIntensity(intensity);
        pLight->SetFlags(Canvas::LightFlags::Enabled | Canvas::LightFlags::CastsShadows);
        pLight->SetShadowResolution(2048);
        pLight->SetShadowDepthBias(1e-4f, 2.0f, 0.5f);
        const FloatQuaternion q = QuaternionLookingAlong(photonDir, { 0, 0, 1, 0 });
        AttachLight(pLight, FloatVector4(0, 0, 0, 1), q, "SunNode");
    }

    void AddPointLight(const FloatVector4& position,
                       const FloatVector4& color,
                       float intensity, float range,
                       bool wantShadows)
    {
        Gem::TGemPtr<Canvas::XLight> pLight;
        Gem::ThrowGemError(m_pCanvas->CreateLight(Canvas::LightType::Point, &pLight, "PointLight"));
        pLight->SetColor(color);
        pLight->SetIntensity(intensity);
        pLight->SetRange(range);
        // Physically-loose inverse-square falloff.
        pLight->SetAttenuation(1.0f, 0.0f, 1.0f / (0.25f * range * range));
        UINT flags = Canvas::LightFlags::Enabled;
        if (wantShadows) flags |= Canvas::LightFlags::CastsShadows;
        pLight->SetFlags(flags);
        AttachLight(pLight, position, FloatQuaternion{}, "PointNode");
        ++Stats.PointLights;
    }

    void AddSpotLight(const FloatVector4& position,
                      const FloatVector4& direction,
                      const FloatVector4& color,
                      float intensity, float range,
                      float innerAngleDeg, float outerAngleDeg,
                      bool wantShadows)
    {
        Gem::TGemPtr<Canvas::XLight> pLight;
        Gem::ThrowGemError(m_pCanvas->CreateLight(Canvas::LightType::Spot, &pLight, "SpotLight"));
        pLight->SetColor(color);
        pLight->SetIntensity(intensity);
        pLight->SetRange(range);
        pLight->SetAttenuation(1.0f, 0.0f, 1.0f / (0.25f * range * range));
        constexpr float kDegToRad = 3.14159265f / 180.0f;
        pLight->SetSpotAngles(innerAngleDeg * kDegToRad, outerAngleDeg * kDegToRad);
        UINT flags = Canvas::LightFlags::Enabled;
        if (wantShadows) flags |= Canvas::LightFlags::CastsShadows;
        pLight->SetFlags(flags);
        const FloatQuaternion q = QuaternionLookingAlong(direction, { 0, 0, 1, 0 });
        AttachLight(pLight, position, q, "SpotNode");
        ++Stats.SpotLights;
    }

    void AttachLight(Gem::TGemPtr<Canvas::XLight>& pLight,
                     const FloatVector4& position,
                     const FloatQuaternion& rotation,
                     const char* nodeName)
    {
        Gem::TGemPtr<Canvas::XSceneGraphNode> pNode;
        Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&pNode, nodeName));
        pNode->SetLocalTranslation(position);
        pNode->SetLocalRotation(rotation);
        pNode->BindElement(pLight);
        m_pScene->GetRootNode()->AddChild(pNode);
        m_Lights.emplace_back(std::move(pLight));
        m_Nodes .emplace_back(std::move(pNode));
    }
};

// ------------------------------------------------------------------
// CApp - window, device, scene, render loop.  Slim version of the
// CApp used by CanvasModelViewer; FBX path removed, procedural scene
// inlined.
// ------------------------------------------------------------------
class CApp
{
    std::string m_Title;
    HINSTANCE   m_hInstance;
    Gem::TGemPtr<Canvas::XLogger> m_pLogger;
    Gem::TGemPtr<Canvas::Platform::Win32::XAppWindow> m_pWindow;
    Gem::TGemPtr<Canvas::Platform::Win32::XRawInput>  m_pInput;
    Gem::TGemPtr<Canvas::XCanvas>           m_pCanvas;
    Gem::TGemPtr<Canvas::XCanvasPlugin>     m_pGfxPlugin;
    Gem::TGemPtr<Canvas::XGfxDevice>        m_pDevice;
    Gem::TGemPtr<Canvas::XGfxRenderQueue>   m_pRenderQueue;
    Gem::TGemPtr<Canvas::XGfxSwapChain>     m_pSwapChain;
    Gem::TGemPtr<Canvas::XScene>            m_pScene;
    Gem::TGemPtr<Canvas::XCamera>           m_pCamera;
    Gem::TGemPtr<Canvas::XSceneGraphNode>   m_pCameraNode;

    Gem::TGemPtr<Canvas::XFont> m_pFont;
    Gem::TGemPtr<Canvas::XFont> m_pFontMono;
    Gem::TGemPtr<Canvas::XUIGraph> m_pUIGraph;
    Gem::TGemPtr<Canvas::XUIRectElement> m_pHudPanel;
    Gem::TGemPtr<Canvas::XUITextElement> m_pTitleText;
    Gem::TGemPtr<Canvas::XUITextElement> m_pStatsText;
    Gem::TGemPtr<Canvas::XUITextElement> m_pFpsText;

    std::unique_ptr<GardenBuilder> m_pGarden;

    GardenConfig m_GardenConfig;
    int m_exitFrameCount;
    bool m_startFullscreen;
    bool m_cameraActive = true;
    int m_lastClientW = 0, m_lastClientH = 0;

    float m_CameraYaw = 0.0f;
    float m_CameraPitch = 0.0f;
    float m_fps = 0.0f;
    std::string m_fpsString;

public:
    CApp(HINSTANCE hInstance, PCSTR title,
         Gem::TGemPtr<Canvas::XLogger> pLogger,
         const GardenConfig& gardenConfig,
         int exitFrameCount, bool fullscreen)
        : m_Title(title), m_hInstance(hInstance), m_pLogger(std::move(pLogger)),
          m_GardenConfig(gardenConfig),
          m_exitFrameCount(exitFrameCount), m_startFullscreen(fullscreen) {}

    bool Initialize(int nCmdShow)
    {
        const char* step = "startup";
        try
        {
            step = "create_window";
            Canvas::Platform::Win32::AppWindowDesc wdesc;
            wdesc.Title     = m_Title.c_str();
            wdesc.hInstance = m_hInstance;
            wdesc.Width     = 1280;
            wdesc.Height    = 768;
            Gem::ThrowGemError(Canvas::Platform::Win32::CreatePlatformWindow(wdesc, &m_pWindow, &m_pInput));
            m_pWindow->Show(nCmdShow);
            if (m_startFullscreen)
                m_pWindow->SetFullscreen(true);

            step = "create_canvas";
            Gem::ThrowGemError(Canvas::CreateCanvas(m_pLogger.Get(), &m_pCanvas));

            step = "load_gfx_plugin";
            Gem::ThrowGemError(m_pCanvas->LoadPlugin("CanvasGfx12.dll", &m_pGfxPlugin));

            step = "create_device";
            Gem::ThrowGemError(m_pGfxPlugin->CreateCanvasElement(
                m_pCanvas, Canvas::TypeId::TypeId_GfxDevice,
                "MainDevice", Canvas::XGfxDevice::IId,
                reinterpret_cast<void**>(&m_pDevice)));

            step = "create_scene";
            Gem::ThrowGemError(m_pCanvas->CreateScene(m_pDevice, &m_pScene, "LightGarden"));

            step = "create_render_queue";
            Gem::ThrowGemError(m_pDevice->CreateRenderQueue(&m_pRenderQueue));

            step = "create_swap_chain";
            Gem::ThrowGemError(m_pRenderQueue->CreateSwapChain(
                m_pWindow->GetHWND(), true, &m_pSwapChain,
                Canvas::GfxFormat::R16G16B16A16_Float, 4));

            step = "create_camera";
            CreateCamera();

            step = "build_scene";
            m_pGarden = std::make_unique<GardenBuilder>(
                m_pCanvas.Get(), m_pDevice.Get(), m_pScene.Get(), m_pLogger.Get());
            m_pGarden->Build(m_GardenConfig);

            // Pre-build the BVH so we can log its size up front (and
            // shift the cost off of frame 1).
            m_pScene->BuildBVH();

            step = "load_fonts";
            LoadFontsAndUi();
            return true;
        }
        catch (const Gem::GemError& e)
        {
            Canvas::LogError(m_pLogger.Get(), "Initialize failed at '%s': %s",
                step, GemResultString(e.Result()));
            return false;
        }
        catch (const std::exception& e)
        {
            Canvas::LogError(m_pLogger.Get(), "Initialize failed at '%s': %s", step, e.what());
            return false;
        }
    }

    int Execute()
    {
        using clock = std::chrono::high_resolution_clock;

        struct CursorGuard {
            Canvas::Platform::Win32::XAppWindow* pWindow;
            ~CursorGuard() { pWindow->SetMouseCaptured(false); }
        } cursorGuard{ m_pWindow.Get() };

        m_pWindow->SetMouseCaptured(true);

        auto prevTime = clock::time_point{};
        auto fpsTime  = clock::now();
        size_t fpsCounter = 0;
        int frameCount = 0;

        for (bool running = true; running;)
        {
            auto currTime = clock::now();
            float dt = (prevTime != clock::time_point{})
                ? std::chrono::duration<float>(currTime - prevTime).count() : 0.0f;
            prevTime = currTime;

            float fpsDt = std::chrono::duration<float>(currTime - fpsTime).count();
            if (fpsDt > 0.5f)
            {
                m_fps = static_cast<float>(fpsCounter) / fpsDt;
                fpsTime = currTime;
                fpsCounter = 0;
            }
            ++fpsCounter;
            ++frameCount;

            HandleResize();
            HandleInputAndCamera(dt);

            if (m_pInput->IsKeyDown(VK_ESCAPE))
                running = false;

            m_pRenderQueue->BeginFrame(m_pSwapChain);
            m_pScene->Update(dt);
            m_pScene->SubmitRenderables(m_pRenderQueue);

            UpdateHud();
            m_pUIGraph->Update();
            m_pUIGraph->SubmitRenderables(m_pRenderQueue);

            m_pRenderQueue->EndFrame();
            m_pRenderQueue->FlushAndPresent(m_pSwapChain);

            if (m_exitFrameCount > 0 && frameCount >= m_exitFrameCount)
            {
                Canvas::LogInfo(m_pLogger.Get(), "Auto-exit after %d frames", frameCount);
                running = false;
            }

            if (!m_pWindow->PumpMessages())
                running = false;
        }
        return 0;
    }

private:
    void CreateCamera()
    {
        Gem::ThrowGemError(m_pCanvas->CreateCamera(&m_pCamera, "MainCamera"));
        m_pCamera->SetNearClip(0.2f);
        m_pCamera->SetFarClip(500.0f);
        m_pCamera->SetFovAngle(static_cast<float>(Canvas::Math::Pi / 4.0));

        Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&m_pCameraNode, "MainCameraNode"));
        m_pCameraNode->BindElement(m_pCamera);

        // Spawn well outside the grid for a wide opening view.
        const float W = m_GardenConfig.CellMeters * m_GardenConfig.GridSize;
        const FloatVector4 pos(-W * 0.6f, -W * 0.6f, W * 0.25f, 0.0f);
        m_pCameraNode->SetLocalTranslation(pos);

        const FloatVector4 forward = (FloatVector4(0, 0, 0, 0) - pos).Normalize();
        m_pCameraNode->SetLocalRotation(QuaternionLookingAlong(forward, { 0, 0, 1, 0 }));

        m_CameraYaw   = std::atan2(forward.Y, forward.X);
        m_CameraPitch = std::asin(std::clamp(forward.Z, -1.0f, 1.0f));

        m_pScene->GetRootNode()->AddChild(m_pCameraNode);
        m_pScene->SetActiveCamera(m_pCamera);
    }

    void LoadFontsAndUi()
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path fontsDir =
            std::filesystem::path(exePath).parent_path() / "fonts";

        auto LoadFont = [&](const std::filesystem::path& path, PCSTR name) {
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (!f.is_open())
            {
                Canvas::LogError(m_pLogger.Get(), "Cannot open font: %s", path.string().c_str());
                Gem::ThrowGemError(Gem::Result::NotFound);
            }
            size_t size = static_cast<size_t>(f.tellg());
            f.seekg(0);
            std::vector<uint8_t> data(size);
            f.read(reinterpret_cast<char*>(data.data()), size);
            Gem::TGemPtr<Canvas::XFont> pF;
            Gem::ThrowGemError(m_pCanvas->CreateFont(data.data(), data.size(), name, &pF));
            return pF;
        };

        m_pFont     = LoadFont(fontsDir / "Inter-Regular.ttf",         "Inter");
        m_pFontMono = LoadFont(fontsDir / "JetBrainsMono-Regular.ttf", "JetBrainsMono");

        Gem::ThrowGemError(m_pCanvas->CreateUIGraph(m_pDevice, &m_pUIGraph));

        Gem::TGemPtr<Canvas::XUIGraphNode> pHud;
        Gem::ThrowGemError(m_pUIGraph->CreateNode(nullptr, &pHud));
        pHud->SetLocalPosition(FloatVector2(6.0f, 6.0f));

        Gem::ThrowGemError(m_pDevice->CreateRectElement(&m_pHudPanel));
        pHud->BindElement(m_pHudPanel);
        m_pHudPanel->SetSize(FloatVector2(360.0f, 96.0f));
        m_pHudPanel->SetFillColor(FloatVector4(0.125f, 0.125f, 0.125f, 0.75f));

        Gem::ThrowGemError(m_pDevice->CreateTextElement(&m_pTitleText));
        pHud->BindElement(m_pTitleText);
        m_pTitleText->SetLocalOffset(FloatVector2(4.0f, 4.0f));
        m_pTitleText->SetFont(m_pFont);
        Canvas::TextLayoutConfig titleCfg;
        titleCfg.FontSize = 28.0f;
        titleCfg.Color = FloatVector4(1, 1, 1, 1);
        m_pTitleText->SetLayoutConfig(titleCfg);
        m_pTitleText->SetText("Canvas Light Garden");

        Gem::ThrowGemError(m_pDevice->CreateTextElement(&m_pStatsText));
        pHud->BindElement(m_pStatsText);
        m_pStatsText->SetLocalOffset(FloatVector2(4.0f, 38.0f));
        m_pStatsText->SetFont(m_pFontMono);
        Canvas::TextLayoutConfig monoCfg;
        monoCfg.FontSize = 16.0f;
        monoCfg.Color = FloatVector4(0.85f, 0.85f, 0.85f, 1);
        m_pStatsText->SetLayoutConfig(monoCfg);
        const auto& s = m_pGarden->Stats;
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "grid=%ux%u  instances=%u  point=%u  spot=%u",
                 m_GardenConfig.GridSize, m_GardenConfig.GridSize,
                 s.MeshInstances, s.PointLights, s.SpotLights);
        m_pStatsText->SetText(buf);

        Gem::ThrowGemError(m_pDevice->CreateTextElement(&m_pFpsText));
        pHud->BindElement(m_pFpsText);
        m_pFpsText->SetLocalOffset(FloatVector2(4.0f, 62.0f));
        m_pFpsText->SetFont(m_pFontMono);
        monoCfg.Color = FloatVector4(0.5f, 1.0f, 0.5f, 1);
        m_pFpsText->SetLayoutConfig(monoCfg);
        m_pFpsText->SetText("FPS: --");
    }

    void HandleResize()
    {
        int cw = 0, ch = 0;
        m_pWindow->GetClientSize(cw, ch);
        if (cw > 0 && ch > 0 && (cw != m_lastClientW || ch != m_lastClientH))
        {
            m_pSwapChain->ResizeBuffers(static_cast<uint32_t>(cw), static_cast<uint32_t>(ch));
            if (m_pCamera)
                m_pCamera->SetAspectRatio(static_cast<float>(cw) / static_cast<float>(ch));
            m_lastClientW = cw;
            m_lastClientH = ch;
        }
    }

    void HandleInputAndCamera(float dt)
    {
        const bool hasFocus = m_pWindow->HasFocus();

        if (hasFocus && m_pInput->IsKeyDown(VK_MENU) && m_pInput->IsKeyPressed(VK_RETURN))
            m_pWindow->SetFullscreen(!m_pWindow->IsFullscreen());

        if (hasFocus && m_pInput->IsKeyPressed(VK_TAB))
            m_cameraActive = !m_cameraActive;

        const bool wantCapture = hasFocus && m_cameraActive;
        if (wantCapture && !m_pWindow->IsMouseCaptured())
            m_pWindow->SetMouseCaptured(true);
        else if (!wantCapture && m_pWindow->IsMouseCaptured())
            m_pWindow->SetMouseCaptured(false);

        if (!hasFocus || !m_cameraActive)
            return;

        float dx = 0, dy = 0;
        m_pInput->GetMouseDelta(dx, dy);

        constexpr float kMouseSens = 0.003f;
        constexpr float kPitchLimit = static_cast<float>(Canvas::Math::Pi / 2.0) - 0.01f;
        constexpr float kMoveSpeed = 8.0f;

        m_CameraYaw   -= dx * kMouseSens;
        m_CameraPitch -= dy * kMouseSens;
        m_CameraPitch = std::clamp(m_CameraPitch, -kPitchLimit, kPitchLimit);

        const float cy = std::cos(m_CameraYaw), sy = std::sin(m_CameraYaw);
        const float cp = std::cos(m_CameraPitch), sp = std::sin(m_CameraPitch);
        const FloatVector4 forward(cy * cp, sy * cp, sp, 0);
        const FloatVector4 worldUp(0, 0, 1, 0);

        FloatMatrix4x4 rotMat = Canvas::Math::IdentityMatrix<float, 4, 4>();
        rotMat[0] = forward;
        Canvas::Math::ComposePointToBasisVectors(worldUp, forward, rotMat[1], rotMat[2]);
        const FloatVector4 right = rotMat[1];

        FloatVector4 move(0, 0, 0, 0);
        if (m_pInput->IsKeyDown('W'))        move = move + forward;
        if (m_pInput->IsKeyDown('S'))        move = move - forward;
        if (m_pInput->IsKeyDown('A'))        move = move + right;
        if (m_pInput->IsKeyDown('D'))        move = move - right;
        if (m_pInput->IsKeyDown(VK_SPACE))   move = move + FloatVector4(0, 0, 1, 0);
        if (m_pInput->IsKeyDown(VK_CONTROL)) move = move - FloatVector4(0, 0, 1, 0);

        const float mag = std::sqrt(Canvas::Math::DotProduct(move, move));
        if (mag > 1e-4f)
            move = move * (kMoveSpeed * dt / mag);

        FloatVector4 pos = m_pCameraNode->GetLocalTranslation();
        pos = pos + move;
        m_pCameraNode->SetLocalTranslation(pos);
        m_pCameraNode->SetLocalRotation(Canvas::Math::QuaternionFromRotationMatrix(rotMat));
    }

    void UpdateHud()
    {
        if (m_fps > 0.0f)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "FPS: %.1f", m_fps);
            if (m_fpsString != buf)
            {
                m_fpsString = buf;
                m_pFpsText->SetText(buf);
            }
        }
    }
};

} // namespace

int APIENTRY WinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPSTR lpCmdLine,
                     _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comOwned = SUCCEEDED(comHr);

    // ---- Parse command line --------------------------------------
    Canvas::CTokenParser tokenParser(lpCmdLine);
    std::vector<const char*> argv;
    argv.push_back("CanvasLightGarden");
    for (size_t i = 0; i < tokenParser.GetTokenCount(); ++i)
        argv.push_back(tokenParser[i]);
    int argc = static_cast<int>(argv.size());

    GardenConfig gardenConfig;
    int exitFrameCount = -1;
    bool fullscreen = false;
    bool logConsole = false;
    std::string logLevel = "warn";

    try
    {
        InCommand::CommandParser parser("CanvasLightGarden");
        auto& root = parser.GetAppCommandDecl();
        root.SetDescription("Canvas procedural many-light sample");

        int gridSizeOpt = static_cast<int>(gardenConfig.GridSize);
        int seedOpt     = static_cast<int>(gardenConfig.Seed);

        root.AddOption(InCommand::OptionType::Variable, "gridsize")
            .SetDescription("Grid cells per side (default 8)").BindTo(gridSizeOpt);
        root.AddOption(InCommand::OptionType::Variable, "seed")
            .SetDescription("PRNG seed (default 0xC0FFEE)").BindTo(seedOpt);
        root.AddOption(InCommand::OptionType::Variable, "exitframecount")
            .SetDescription("Exit after N frames (testing)").BindTo(exitFrameCount);
        root.AddOption(InCommand::OptionType::Switch, "fullscreen")
            .SetDescription("Start in borderless fullscreen").BindTo(fullscreen);
        root.AddOption(InCommand::OptionType::Switch, "shadows-all")
            .SetDescription("Request shadows on every light (no-op until engine supports point/spot shadows)")
            .BindTo(gardenConfig.ShadowsAll);

        auto& logCmd = root.AddSubCommand("log");
        logCmd.AddOption(InCommand::OptionType::Variable, "level", 'l')
            .SetDomain({"trace","debug","info","warn","error","critical","off"}).BindTo(logLevel);
        logCmd.AddOption(InCommand::OptionType::Switch, "console", 'c').BindTo(logConsole);

        std::ostringstream helpStream;
        parser.EnableAutoHelp("help", 'h', helpStream);
        parser.ParseArgs(argc, argv.data());

        if (parser.WasAutoHelpRequested())
        {
            AllocConsole();
            SetConsoleTitleA("CanvasLightGarden - Help");
            FILE* tmpOut = nullptr;
            freopen_s(&tmpOut, "CONOUT$", "w", stdout);
            std::cout << helpStream.str() << std::endl;
            std::cout << "Press any key to close..." << std::endl;
            (void)_getch();
            if (tmpOut) fclose(tmpOut);
            FreeConsole();
            if (comOwned) CoUninitialize();
            return 0;
        }

        gardenConfig.GridSize = static_cast<uint32_t>(std::max(1, gridSizeOpt));
        gardenConfig.Seed     = static_cast<uint32_t>(seedOpt);
    }
    catch (const InCommand::SyntaxException& e)
    {
        MessageBoxA(nullptr, e.GetMessage().c_str(), "CanvasLightGarden", MB_OK | MB_ICONERROR);
        if (comOwned) CoUninitialize();
        return -1;
    }

    // ---- Logger --------------------------------------------------
    QLog::Level qlogLevel = QLog::Level::Warn;
    if      (logLevel == "trace")    qlogLevel = QLog::Level::Trace;
    else if (logLevel == "debug")    qlogLevel = QLog::Level::Debug;
    else if (logLevel == "info")     qlogLevel = QLog::Level::Info;
    else if (logLevel == "error")    qlogLevel = QLog::Level::Error;
    else if (logLevel == "critical") qlogLevel = QLog::Level::Critical;
    else if (logLevel == "off")      qlogLevel = QLog::Level::Off;

    wchar_t exeBuf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{}; localtime_s(&tm, &tt);
    char tbuf[32]; std::strftime(tbuf, sizeof(tbuf), "%Y%m%d%H%M%S", &tm);
    auto logPath = std::filesystem::path(exeBuf).parent_path()
                 / (std::string("CanvasLightGarden_") + tbuf + ".log");

    std::unique_ptr<CConsole> pConsole;
    if (logConsole) pConsole = std::make_unique<CConsole>();

    CLogSink sink(logPath, logConsole);
    auto qlogger = std::make_unique<QLog::Logger>(sink, qlogLevel);

    Gem::TGemPtr<Canvas::QLogAdapter> pAdapter;
    Gem::ThrowGemError(Gem::TGenericImpl<Canvas::QLogAdapter>::Create(&pAdapter, qlogger.release()));
    Gem::TGemPtr<Canvas::XLogger> pLogger;
    pAdapter->QueryInterface(&pLogger);

    Canvas::LogInfo(pLogger.Get(), "CanvasLightGarden starting: grid=%u seed=0x%08X",
                    gardenConfig.GridSize, gardenConfig.Seed);
    Canvas::LogInfo(pLogger.Get(), "Log file: %s", logPath.string().c_str());

    int result = 0;
    {
        CApp app(hInstance, "CanvasLightGarden",
                 pLogger, gardenConfig, exitFrameCount, fullscreen);
        if (!app.Initialize(nCmdShow))
        {
            MessageBoxA(nullptr, "Initialization failed; see log for details.",
                        "CanvasLightGarden", MB_OK | MB_ICONERROR);
            result = -1;
        }
        else
        {
            result = app.Execute();
        }
    }

    pConsole.reset();
    if (comOwned) CoUninitialize();
    return result;
}
