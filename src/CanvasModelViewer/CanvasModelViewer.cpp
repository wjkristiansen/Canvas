// CanvasModelViewer.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "CanvasModelViewer.h"
#include "CanvasCore.h"
#include "CanvasGfx.h"
#include "QLogAdapter.h"
#include "TokenParser.h"
#include "InCommand.h"
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cmath>

//------------------------------------------------------------------------------------------------
// D3D12 Agility SDK version exports - required to activate newer D3D12 APIs
// These should be exported from the main executable for best compatibility
//------------------------------------------------------------------------------------------------
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 616; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

class CConsole
{
    FILE *m_stdin;
    FILE *m_stdout;
    FILE *m_stderr;
    bool m_attachedToParent;
    bool m_filesOpened;

public:
    CConsole()
        : m_stdin(nullptr)
        , m_stdout(nullptr)
        , m_stderr(nullptr)
        , m_attachedToParent(false)
        , m_filesOpened(false)
    {
        // Try to attach to parent console first (if launched from console)
        if (AttachConsole(ATTACH_PARENT_PROCESS))
        {
            // Check if stdout handle is actually valid and usable
            HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD mode = 0;
            
            // GetConsoleMode succeeds only if it's a real console (not redirected/piped)
            if (hStdOut != INVALID_HANDLE_VALUE && GetConsoleMode(hStdOut, &mode))
            {
                // Valid console - use it
                m_attachedToParent = true;
            }
            else
            {
                // Console exists but may not be visible (e.g., VS Code debugger with redirected I/O)
                // Detach and create our own visible console
                FreeConsole();
                AllocConsole();
                m_attachedToParent = false;
            }
        }
        else
        {
            // No parent console, create our own
            AllocConsole();
            m_attachedToParent = false;
        }

        // Reopen standard streams to use the console
        if (freopen_s(&m_stdin, "CONIN$", "r", stdin) == 0 &&
            freopen_s(&m_stdout, "CONOUT$", "w", stdout) == 0 &&
            freopen_s(&m_stderr, "CONOUT$", "w", stderr) == 0)
        {
            m_filesOpened = true;
        }
    }

    ~CConsole()
    {
        if (m_filesOpened)
        {
            if (m_stdin) fclose(m_stdin);
            if (m_stdout) fclose(m_stdout);
            if (m_stderr) fclose(m_stderr);
        }

        // Only free console if we created it (not if we attached to parent)
        if (!m_attachedToParent)
        {
            FreeConsole();
        }
    }

    bool IsAttachedToParent() const { return m_attachedToParent; }
};

//------------------------------------------------------------------------------------------------
class CLogSink : public QLog::Sink
{
    void OutputString(PCSTR sz)
    {
        OutputDebugStringA(sz); // Debugger
        fputs(sz, stdout); // Console
    }

    void Write(const QLog::Message& message) override
    {
        // Use QLog's FormatTimestamp helper for clean, consistent formatting
        std::ostringstream oss;
        
        oss << QLog::FormatTimestamp(message) << QLog::ToString(message.level) << ": " << message.text << '\n';
        
        std::string formatted = oss.str();
        OutputString(formatted.c_str());
    }
};

//------------------------------------------------------------------------------------------------
class CApp
{
    std::string m_Title;
    HINSTANCE m_hInstance;
    Gem::TGemPtr<Canvas::XLogger> m_pLogger;
    std::unique_ptr<ThinWin::CWindow> m_pWindow;
    Gem::TGemPtr<Canvas::XCanvas> m_pCanvas;
    Gem::TGemPtr<Canvas::XScene> m_pScene;
    Gem::TGemPtr<Canvas::XCamera> m_pCamera;
    Gem::TGemPtr<Canvas::XCanvasPlugin> m_pGfxPlugin;
    Gem::TGemPtr<Canvas::XGfxDevice> m_pGfxDevice;
    Gem::TGemPtr<Canvas::XGfxSwapChain> m_pGfxSwapChain;
    Gem::TGemPtr<Canvas::XGfxRenderQueue> m_pGfxRenderQueue;
    Gem::TGemPtr<Canvas::XGfxMeshData> m_pCubeMesh;
    Gem::TGemPtr<Canvas::XMeshInstance> m_pCubeMeshInstance;
    Gem::TGemPtr<Canvas::XSceneGraphNode> m_pDebugCubeNode;
    Gem::TGemPtr<Canvas::XFont> m_pFont;
    Gem::TGemPtr<Canvas::XFont> m_pFontMono;
    Gem::TGemPtr<Canvas::XLight> m_pSunLight;
    Gem::TGemPtr<Canvas::XLight> m_pAmbientLight;
    Gem::TGemPtr<Canvas::XUIGraph> m_pUIGraph;
    Gem::TGemPtr<Canvas::XUITextElement> m_pTitleText;
    Gem::TGemPtr<Canvas::XUITextElement> m_pFpsText;
    int m_exitFrameCount;  // -1 means don't exit automatically; >= 0 means exit after N frames
    float m_fps = 0.0f;
    std::string m_fpsString;

    // Camera controller state
    float m_CameraYaw = 0.0f;    // Radians, around world Z (up)
    float m_CameraPitch = 0.0f;  // Radians, around camera right
    bool m_MouseCaptured = false;
    POINT m_LastCursorPos = {};

public:
    CApp(HINSTANCE hInstance, PCSTR szTitle, Gem::TGemPtr<Canvas::XLogger> pLogger, int exitFrameCount = -1) :
        m_pLogger(pLogger),
        m_Title(szTitle),
        m_hInstance(hInstance),
        m_exitFrameCount(exitFrameCount)
        {
        }

    ~CApp() 
    {
    };

    bool Initialize(int nCmdShow)
    {
        Canvas::CFunctionSentinel sentinel("CApp::Initialize", m_pLogger.Get());

        // Construct the CWindow
        try
        {
            std::unique_ptr<ThinWin::CWindow> pWindow = std::make_unique<ThinWin::CWindow>(m_Title.c_str(), m_hInstance, WS_OVERLAPPEDWINDOW);
            if (!pWindow.get())
            {
                Gem::ThrowGemError(Gem::Result::OutOfMemory);
            }

            pWindow->ShowWindow(nCmdShow);
            pWindow->UpdateWindow();

            Gem::TGemPtr<Canvas::XCanvas> pCanvas;
            Gem::ThrowGemError(Canvas::CreateCanvas(m_pLogger.Get(), &pCanvas));

            Gem::TGemPtr<Canvas::XScene> pScene;
            Gem::ThrowGemError(pCanvas->CreateScene(&pScene, "MainScene"));

            bool Windowed = true;
            UINT WidthIfWindowed = 1280;
            UINT HeightIfWindowed = 768;

            if (Windowed)
            {
                // Resize the window
                RECT rcWnd;
                GetWindowRect(pWindow->m_hWnd, &rcWnd);
                SetWindowPos(pWindow->m_hWnd, NULL, rcWnd.left, rcWnd.top, WidthIfWindowed, HeightIfWindowed, SWP_NOZORDER);
            }

            // Load the graphics plugin
            Gem::TGemPtr<Canvas::XCanvasPlugin> pGfxPlugin;
            Gem::ThrowGemError(pCanvas->LoadPlugin("CanvasGfx12.dll", &pGfxPlugin));

            // Create the graphics device
            Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
            Gem::ThrowGemError(pGfxPlugin->CreateCanvasElement(
                pCanvas,
                Canvas::TypeId::TypeId_GfxDevice,
                "MainDevice",
                Canvas::XGfxDevice::IId,
                (void**)&pDevice));

            // Create the render queue
            Gem::TGemPtr<Canvas::XGfxRenderQueue> pGfxRenderQueue;
            Gem::ThrowGemError(pDevice->CreateRenderQueue(&pGfxRenderQueue));

            // Create the swapchain
            Gem::TGemPtr<Canvas::XGfxSwapChain> pSwapChain;
            Gem::ThrowGemError(pGfxRenderQueue->CreateSwapChain(pWindow->m_hWnd, true, &pSwapChain, Canvas::GfxFormat::R16G16B16A16_Float, 4));

            Gem::TGemPtr<Canvas::XCamera> pCamera;
            Gem::ThrowGemError(pCanvas->CreateCamera(&pCamera, "MainCamera"));
            pCamera->SetName("MainCamera");
            pCamera->SetNearClip(0.1f);
            pCamera->SetFarClip(1000.f);
            pCamera->SetFovAngle(float(Canvas::Math::Pi / 4));
            Gem::TGemPtr<Canvas::XSceneGraphNode> pCameraNode;
            Gem::ThrowGemError(pCanvas->CreateSceneGraphNode(&pCameraNode, "MainCameraNode"));
            pCameraNode->SetName("MainCameraNode");
            Gem::ThrowGemError(pCameraNode->BindElement(pCamera));
            
            // Position the camera
            Canvas::Math::FloatVector4 cameraPosition(1.0f, -2.0f, 1.0f, 0.0f);
            pCameraNode->SetLocalTranslation(cameraPosition);
            
            // Create a look-at rotation to face the origin
            // Camera is at (0, -2, 1), looking at origin (0, 0, 0)
            Canvas::Math::FloatVector4 basisForward =
                (Canvas::Math::FloatVector4(0.0f, 0.0f, 0.0f, 0.0f) - cameraPosition).Normalize();
            
            // World up is Z-up
            Canvas::Math::FloatVector4 worldUp(0.0f, 0.0f, 1.0f, 0.0f); // Z-up world
            
            // Build rotation matrix and compose basis vectors directly into rows
            Canvas::Math::FloatMatrix4x4 rotationMatrix = Canvas::Math::IdentityMatrix<float, 4, 4>();
            rotationMatrix[0] = basisForward;
            Canvas::Math::ComposePointToBasisVectors(worldUp, basisForward, rotationMatrix[1], rotationMatrix[2]);

            // Convert rotation matrix to quaternion
            Canvas::Math::FloatQuaternion cameraRotation = Canvas::Math::QuaternionFromRotationMatrix(rotationMatrix);
            pCameraNode->SetLocalRotation(cameraRotation);

            // Initialize camera yaw/pitch from the look direction
            m_CameraYaw = atan2f(basisForward[1], basisForward[0]);
            m_CameraPitch = asinf(std::clamp(basisForward[2], -1.0f, 1.0f));
            
            pScene->GetRootSceneGraphNode()->AddChild(pCameraNode);

            // Create a unit cube mesh centered at the origin
            // 6 faces × 2 triangles × 3 vertices = 36 vertices
            // Each face has its own normal (face normal), so vertices are not shared between faces
            const float h = 0.5f; // half-edge length for unit cube
            
            // Define the 8 corners of the cube
            // In Z-up coordinate system: X=right, Y=forward, Z=up
            Canvas::Math::FloatVector4 corners[8] = {
                Canvas::Math::FloatVector4(-h, -h, -h, 1.0f), // 0: left-back-bottom
                Canvas::Math::FloatVector4( h, -h, -h, 1.0f), // 1: right-back-bottom
                Canvas::Math::FloatVector4( h,  h, -h, 1.0f), // 2: right-front-bottom
                Canvas::Math::FloatVector4(-h,  h, -h, 1.0f), // 3: left-front-bottom
                Canvas::Math::FloatVector4(-h, -h,  h, 1.0f), // 4: left-back-top
                Canvas::Math::FloatVector4( h, -h,  h, 1.0f), // 5: right-back-top
                Canvas::Math::FloatVector4( h,  h,  h, 1.0f), // 6: right-front-top
                Canvas::Math::FloatVector4(-h,  h,  h, 1.0f), // 7: left-front-top
            };
            
            // Define face normals
            Canvas::Math::FloatVector4 normalPosX( 1.0f,  0.0f,  0.0f, 0.0f); // +X face (right)
            Canvas::Math::FloatVector4 normalNegX(-1.0f,  0.0f,  0.0f, 0.0f); // -X face (left)
            Canvas::Math::FloatVector4 normalPosY( 0.0f,  1.0f,  0.0f, 0.0f); // +Y face (front)
            Canvas::Math::FloatVector4 normalNegY( 0.0f, -1.0f,  0.0f, 0.0f); // -Y face (back)
            Canvas::Math::FloatVector4 normalPosZ( 0.0f,  0.0f,  1.0f, 0.0f); // +Z face (top)
            Canvas::Math::FloatVector4 normalNegZ( 0.0f,  0.0f, -1.0f, 0.0f); // -Z face (bottom)
            
            // 36 vertices: 6 faces × 2 triangles × 3 vertices
            Canvas::Math::FloatVector4 cubePositions[36];
            Canvas::Math::FloatVector4 cubeNormals[36];
            
            int v = 0;
            
            // +X face (right): corners 1, 2, 6, 5 (CCW when viewed from +X)
            cubePositions[v] = corners[1]; cubeNormals[v++] = normalPosX;
            cubePositions[v] = corners[2]; cubeNormals[v++] = normalPosX;
            cubePositions[v] = corners[6]; cubeNormals[v++] = normalPosX;
            cubePositions[v] = corners[1]; cubeNormals[v++] = normalPosX;
            cubePositions[v] = corners[6]; cubeNormals[v++] = normalPosX;
            cubePositions[v] = corners[5]; cubeNormals[v++] = normalPosX;
            
            // -X face (left): corners 0, 4, 7, 3 (CCW when viewed from -X)
            cubePositions[v] = corners[0]; cubeNormals[v++] = normalNegX;
            cubePositions[v] = corners[4]; cubeNormals[v++] = normalNegX;
            cubePositions[v] = corners[7]; cubeNormals[v++] = normalNegX;
            cubePositions[v] = corners[0]; cubeNormals[v++] = normalNegX;
            cubePositions[v] = corners[7]; cubeNormals[v++] = normalNegX;
            cubePositions[v] = corners[3]; cubeNormals[v++] = normalNegX;
            
            // +Y face (front): corners 2, 3, 7, 6 (CCW when viewed from +Y)
            cubePositions[v] = corners[2]; cubeNormals[v++] = normalPosY;
            cubePositions[v] = corners[3]; cubeNormals[v++] = normalPosY;
            cubePositions[v] = corners[7]; cubeNormals[v++] = normalPosY;
            cubePositions[v] = corners[2]; cubeNormals[v++] = normalPosY;
            cubePositions[v] = corners[7]; cubeNormals[v++] = normalPosY;
            cubePositions[v] = corners[6]; cubeNormals[v++] = normalPosY;
            
            // -Y face (back): corners 0, 1, 5, 4 (CCW when viewed from -Y)
            cubePositions[v] = corners[0]; cubeNormals[v++] = normalNegY;
            cubePositions[v] = corners[1]; cubeNormals[v++] = normalNegY;
            cubePositions[v] = corners[5]; cubeNormals[v++] = normalNegY;
            cubePositions[v] = corners[0]; cubeNormals[v++] = normalNegY;
            cubePositions[v] = corners[5]; cubeNormals[v++] = normalNegY;
            cubePositions[v] = corners[4]; cubeNormals[v++] = normalNegY;
            
            // +Z face (top): corners 4, 5, 6, 7 (CCW when viewed from +Z)
            cubePositions[v] = corners[4]; cubeNormals[v++] = normalPosZ;
            cubePositions[v] = corners[5]; cubeNormals[v++] = normalPosZ;
            cubePositions[v] = corners[6]; cubeNormals[v++] = normalPosZ;
            cubePositions[v] = corners[4]; cubeNormals[v++] = normalPosZ;
            cubePositions[v] = corners[6]; cubeNormals[v++] = normalPosZ;
            cubePositions[v] = corners[7]; cubeNormals[v++] = normalPosZ;
            
            // -Z face (bottom): corners 0, 3, 2, 1 (CCW when viewed from -Z)
            cubePositions[v] = corners[0]; cubeNormals[v++] = normalNegZ;
            cubePositions[v] = corners[2]; cubeNormals[v++] = normalNegZ;
            cubePositions[v] = corners[1]; cubeNormals[v++] = normalNegZ;
            cubePositions[v] = corners[0]; cubeNormals[v++] = normalNegZ;
            cubePositions[v] = corners[3]; cubeNormals[v++] = normalNegZ;
            cubePositions[v] = corners[2]; cubeNormals[v++] = normalNegZ;
            
            Gem::TGemPtr<Canvas::XGfxMeshData> pCubeMesh;
            Gem::ThrowGemError(pDevice->CreateDebugMeshData(
                36,
                cubePositions,
                cubeNormals,
                pGfxRenderQueue,
                &pCubeMesh));

            // Create a mesh element and bind the cube mesh to it
            Gem::TGemPtr<Canvas::XMeshInstance> pCube;
            Gem::ThrowGemError(pCanvas->CreateMeshInstance(&pCube, "DebugCubeMeshInstance"));
            pCube->SetMeshData(pCubeMesh);

            // Create a scene graph node for the debug cube and bind the mesh element
            Gem::TGemPtr<Canvas::XSceneGraphNode> pDebugCubeNode;
            Gem::ThrowGemError(pCanvas->CreateSceneGraphNode(&pDebugCubeNode, "DebugCubeNode"));
            Gem::ThrowGemError(pDebugCubeNode->BindElement(pCube));

            // Add the debug cube node to the scene
            pScene->GetRootSceneGraphNode()->AddChild(pDebugCubeNode);

            // Set the active camera for the scene
            pScene->SetActiveCamera(pCamera);

            // Initialize text rendering
            // Fonts are bundled at bin/fonts/ alongside the executable (fetched at CMake configure time).
            Gem::TGemPtr<Canvas::XFont> pFont;
            Gem::TGemPtr<Canvas::XFont> pFontMono;

            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::filesystem::path fontsDir =
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
                Canvas::LogInfo(m_pLogger.Get(), "Loaded font: %s", name);
                return pF;
            };

            pFont     = LoadFont(fontsDir / "Inter-Regular.ttf",         "Inter");
            pFontMono = LoadFont(fontsDir / "JetBrainsMono-Regular.ttf", "JetBrainsMono");

            // Create UI graph for text rendering
            Gem::TGemPtr<Canvas::XUIGraph> pUIGraph;
            Gem::ThrowGemError(pCanvas->CreateUIGraph(pDevice, pGfxRenderQueue, &pUIGraph));

            // Title text element — static, set once
            Gem::TGemPtr<Canvas::XUITextElement> pTitleText;
            Gem::ThrowGemError(pUIGraph->CreateTextElement(pUIGraph->GetRoot(), &pTitleText));
            pTitleText->SetFont(pFont);
            {
Canvas::TextLayoutConfig titleConfig;
            titleConfig.FontSize = 32.0f;
            titleConfig.Color = 0xFFFFFFFF;
            pTitleText->SetLayoutConfig(titleConfig);
            }
pTitleText->SetPosition(Canvas::Math::FloatVector2(10.0f, 10.0f));
            pTitleText->SetText("Canvas Model Viewer");

            // FPS text element — dynamic, updated when value changes
            Gem::TGemPtr<Canvas::XUITextElement> pFpsText;
            Gem::ThrowGemError(pUIGraph->CreateTextElement(pUIGraph->GetRoot(), &pFpsText));
            pFpsText->SetFont(pFontMono);
            {
                Canvas::TextLayoutConfig monoConfig;
                monoConfig.FontSize = 18.0f;
                monoConfig.Color = 0xFFCCCCCC;
                pFpsText->SetLayoutConfig(monoConfig);
            }
            pFpsText->SetPosition(Canvas::Math::FloatVector2(10.0f, 50.0f));
            pFpsText->SetText("FPS: --");

            // Create lights
            Gem::TGemPtr<Canvas::XLight> pSunLight;
            Gem::ThrowGemError(pCanvas->CreateLight(Canvas::LightType::Directional, &pSunLight, "SunLight"));
            pSunLight->SetColor(Canvas::Math::FloatVector4(1.0f, 0.95f, 0.85f, 0.0f));
            pSunLight->SetIntensity(1.0f);

            Gem::TGemPtr<Canvas::XLight> pAmbientLight;
            Gem::ThrowGemError(pCanvas->CreateLight(Canvas::LightType::Ambient, &pAmbientLight, "AmbientLight"));
            pAmbientLight->SetColor(Canvas::Math::FloatVector4(0.15f, 0.15f, 0.2f, 0.0f));
            pAmbientLight->SetIntensity(1.0f);

            // Attach sun light to a node for direction (node's forward row = light direction)
            Gem::TGemPtr<Canvas::XSceneGraphNode> pSunNode;
            Gem::ThrowGemError(pCanvas->CreateSceneGraphNode(&pSunNode, "SunLightNode"));
            Gem::ThrowGemError(pSunNode->BindElement(pSunLight));

            // Set sun direction via node rotation: forward (row 0) = normalized (0.3, 0.5, 0.7)
            {
                Canvas::Math::FloatVector4 sunForward(0.3f, 0.5f, 0.7f, 0.0f);
                sunForward = sunForward.Normalize();
                Canvas::Math::FloatMatrix4x4 sunRotation = Canvas::Math::IdentityMatrix<float, 4, 4>();
                sunRotation[0] = sunForward;
                Canvas::Math::FloatVector4 up(0.0f, 0.0f, 1.0f, 0.0f);
                Canvas::Math::ComposePointToBasisVectors(up, sunForward, sunRotation[1], sunRotation[2]);
                pSunNode->SetLocalRotation(Canvas::Math::QuaternionFromRotationMatrix(sunRotation));
            }
            pScene->GetRootSceneGraphNode()->AddChild(pSunNode);

            // Attach ambient light to a node (no direction needed, but must be in scene graph)
            Gem::TGemPtr<Canvas::XSceneGraphNode> pAmbientNode;
            Gem::ThrowGemError(pCanvas->CreateSceneGraphNode(&pAmbientNode, "AmbientLightNode"));
            Gem::ThrowGemError(pAmbientNode->BindElement(pAmbientLight));
            pScene->GetRootSceneGraphNode()->AddChild(pAmbientNode);

            m_pGfxPlugin.Attach(pGfxPlugin.Detach());
            m_pGfxDevice.Attach(pDevice.Detach());
            m_pGfxRenderQueue.Attach(pGfxRenderQueue.Detach());
            m_pGfxSwapChain.Attach(pSwapChain.Detach());
            m_pCamera.Attach(pCamera.Detach());
            m_pCanvas.Attach(pCanvas.Detach());
            m_pScene.Attach(pScene.Detach());
            m_pCubeMesh.Attach(pCubeMesh.Detach());
            m_pCubeMeshInstance.Attach(pCube.Detach());
            m_pDebugCubeNode.Attach(pDebugCubeNode.Detach());
            m_pFont.Attach(pFont.Detach());
            m_pFontMono.Attach(pFontMono.Detach());
            m_pSunLight.Attach(pSunLight.Detach());
            m_pAmbientLight.Attach(pAmbientLight.Detach());
            m_pUIGraph.Attach(pUIGraph.Detach());
            m_pTitleText.Attach(pTitleText.Detach());
            m_pFpsText.Attach(pFpsText.Detach());

            m_pWindow = std::move(pWindow);

            return true;
        }
        catch (std::bad_alloc &)
        {
            sentinel.SetResultCode(Gem::Result::OutOfMemory);
            return false;
        }
        catch (Gem::GemError &e)
        {
            sentinel.SetResultCode(e.Result());
            return false;
        }
    }

    ThinWin::CWindow *GetWindow() const
    { 
        return m_pWindow.get(); 
    }

    int Execute()
    {
        HACCEL hAccelTable = LoadAccelerators(m_hInstance, MAKEINTRESOURCE(IDC_CANVASMODELVIEWER));

        using clock = std::chrono::high_resolution_clock;
        using time_point = std::chrono::time_point<clock>;

        bool running = true;
        float dtime = 0.0;
        size_t fpsCounter = 0;
        time_point prevTime{};
        time_point fpsTime = clock::now();
        int frameCount = 0;  // Track frames for auto-exit

        // Camera control constants
        constexpr float kMoveSpeed = 3.0f;          // units per second
        constexpr float kMouseSensitivity = 0.003f;  // radians per pixel (~360° over ~2094 px ≈ 8 inches at 96 DPI)
        constexpr float kPitchLimit = static_cast<float>(Canvas::Math::Pi / 2.0) - 0.01f;

        // Capture cursor on startup
        {
            RECT rc;
            GetClientRect(m_pWindow->m_hWnd, &rc);
            POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
            ClientToScreen(m_pWindow->m_hWnd, &center);
            SetCursorPos(center.x, center.y);
            m_LastCursorPos = center;

            // Clip cursor to window and hide it
            GetClientRect(m_pWindow->m_hWnd, &rc);
            MapWindowPoints(m_pWindow->m_hWnd, nullptr, reinterpret_cast<POINT*>(&rc), 2);
            ClipCursor(&rc);
            while (ShowCursor(FALSE) >= 0) {} // hide until count < 0
            m_MouseCaptured = true;
        }

        for (; running;)
        {
            time_point currTime = clock::now();
            if(prevTime != time_point{})
            {
                dtime = std::chrono::duration<float>(currTime - prevTime).count();
            }
            prevTime = currTime;

            float fpsDTime = std::chrono::duration<float>(currTime - fpsTime).count();
            if (fpsDTime > 1.0f)
            {
                m_fps = fpsCounter / fpsDTime;
                Canvas::LogInfo(m_pLogger.Get(), "FPS: %.1f", m_fps);
                fpsTime = currTime;
                fpsCounter = 0;
            }
            ++fpsCounter;
            ++frameCount;

            //==================================================================
            // Camera controller: mouse look + WASD movement
            //==================================================================
            
            // Track focus changes — release/acquire cursor as needed
            bool hasFocus = (GetForegroundWindow() == m_pWindow->m_hWnd);
            if (hasFocus && !m_MouseCaptured)
            {
                // Re-acquire cursor
                RECT rc;
                GetClientRect(m_pWindow->m_hWnd, &rc);
                POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
                ClientToScreen(m_pWindow->m_hWnd, &center);
                SetCursorPos(center.x, center.y);

                MapWindowPoints(m_pWindow->m_hWnd, nullptr, reinterpret_cast<POINT*>(&rc), 2);
                ClipCursor(&rc);
                while (ShowCursor(FALSE) >= 0) {}
                m_MouseCaptured = true;
            }
            else if (!hasFocus && m_MouseCaptured)
            {
                // Release cursor
                ClipCursor(nullptr);
                while (ShowCursor(TRUE) < 0) {}
                m_MouseCaptured = false;
            }

            if (m_MouseCaptured)
            {
                // Mouse delta → yaw/pitch
                RECT rc;
                GetClientRect(m_pWindow->m_hWnd, &rc);
                POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
                ClientToScreen(m_pWindow->m_hWnd, &center);

                POINT cursorPos;
                GetCursorPos(&cursorPos);
                float dx = static_cast<float>(cursorPos.x - center.x);
                float dy = static_cast<float>(cursorPos.y - center.y);
                SetCursorPos(center.x, center.y);

                m_CameraYaw   -= dx * kMouseSensitivity;
                m_CameraPitch -= dy * kMouseSensitivity;
                m_CameraPitch = std::clamp(m_CameraPitch, -kPitchLimit, kPitchLimit);

                // Build camera orientation matrix from yaw/pitch (Z-up world)
                // Canvas row-vector convention: row 0 = forward, row 1 = right, row 2 = up
                float cy = cosf(m_CameraYaw),  sy = sinf(m_CameraYaw);
                float cp = cosf(m_CameraPitch), sp = sinf(m_CameraPitch);

                Canvas::Math::FloatVector4 forward(cy * cp, sy * cp, sp, 0.0f);
                Canvas::Math::FloatVector4 worldUp(0.0f, 0.0f, 1.0f, 0.0f);

                Canvas::Math::FloatMatrix4x4 rotMat = Canvas::Math::IdentityMatrix<float, 4, 4>();
                rotMat[0] = forward;
                Canvas::Math::ComposePointToBasisVectors(worldUp, forward, rotMat[1], rotMat[2]);
                Canvas::Math::FloatQuaternion cameraQuat = Canvas::Math::QuaternionFromRotationMatrix(rotMat);

                // Right vector for WASD strafing (from the rotation matrix)
                Canvas::Math::FloatVector4 right = rotMat[1];

                // WASD movement in camera-local space
                Canvas::Math::FloatVector4 moveDir(0.0f, 0.0f, 0.0f, 0.0f);
                if (GetAsyncKeyState('W') & 0x8000) moveDir = moveDir + forward;
                if (GetAsyncKeyState('S') & 0x8000) moveDir = moveDir - forward;
                if (GetAsyncKeyState('D') & 0x8000) moveDir = moveDir - right;
                if (GetAsyncKeyState('A') & 0x8000) moveDir = moveDir + right;
                if (GetAsyncKeyState(VK_SPACE) & 0x8000)  moveDir = moveDir + Canvas::Math::FloatVector4(0, 0, 1, 0);
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000)  moveDir = moveDir + Canvas::Math::FloatVector4(0, 0, 1, 0);
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) moveDir = moveDir - Canvas::Math::FloatVector4(0, 0, 1, 0);
                if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { running = false; break; }

                float moveMag = sqrtf(Canvas::Math::DotProduct(moveDir, moveDir));
                if (moveMag > 0.001f)
                    moveDir = moveDir * (1.0f / moveMag);

                auto *pCamNode = m_pCamera->GetAttachedNode();
                if (pCamNode)
                {
                    auto pos = pCamNode->GetLocalTranslation();
                    pos = pos + moveDir * (kMoveSpeed * dtime);
                    pCamNode->SetLocalTranslation(pos);
                    pCamNode->SetLocalRotation(cameraQuat);
                }
            }

            // Update scene, submit renderables (including lights), render
            m_pGfxRenderQueue->BeginFrame(m_pGfxSwapChain);
            m_pScene->Update(dtime);
            m_pScene->SubmitRenderables(m_pGfxRenderQueue);
            
            // Update FPS text only when the formatted string changes
            if (m_fps > 0.0f)
            {
                char fpsBuf[32];
                snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %.1f", m_fps);
                if (m_fpsString != fpsBuf)
                {
                    m_fpsString = fpsBuf;
                    m_pFpsText->SetText(fpsBuf);
                }
            }

            m_pUIGraph->Update();
            m_pUIGraph->Submit(m_pGfxRenderQueue);

            m_pGfxRenderQueue->EndFrame();
            m_pGfxRenderQueue->FlushAndPresent(m_pGfxSwapChain);

            // Check if we should exit after N frames
            if (m_exitFrameCount > 0 && frameCount >= m_exitFrameCount)
            {
                Canvas::LogInfo(m_pLogger.Get(), "Auto-exiting after %d frames", frameCount);
                running = false;
                continue;
            }

            // Main message loop:
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }

                if (msg.message == WM_QUIT)
                {
                    running = false;
                }
            }
            // Optionally: log or use elapsedTime and deltaTime here
        }

        // Release cursor on exit
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
        m_MouseCaptured = false;

        return 0;
    }
};

const UINT MAX_LOADSTRING = 100;

// Forward declarations of functions included in this code module:
int APIENTRY WinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPSTR    lpCmdLine,
                     _In_ int       nCmdShow)
    {
    UNREFERENCED_PARAMETER(hPrevInstance);

    // Parse command line arguments using TokenParser and InCommand
    Canvas::CTokenParser tokenParser(lpCmdLine);
    
    // Build argc/argv format for InCommand
    std::vector<const char*> argv;
    argv.push_back("CanvasModelViewer"); // Program name
    for (size_t i = 0; i < tokenParser.GetTokenCount(); ++i)
    {
        argv.push_back(tokenParser[i]);
    }
    int argc = static_cast<int>(argv.size());

    bool noConsole = false;
    std::string logLevel = "info";
    int exitFrameCount = -1;  // -1 means don't exit automatically

    // Create console early - will attach to parent if launched from console, or create new one
    std::unique_ptr<CConsole> pConsole = std::make_unique<CConsole>();
    bool attachedToParent = pConsole->IsAttachedToParent();

    try
    {
        // Setup command line parser
        InCommand::CommandParser cmdParser("CanvasModelViewer");
        auto& rootCmd = cmdParser.GetAppCommandDecl();
        rootCmd.SetDescription("Canvas 3D Model Viewer");
        
        // Add command line options
        rootCmd.AddOption(InCommand::OptionType::Switch, "no-console")
            .SetDescription("Disable console window (ignored when launched from existing console)")
            .BindTo(noConsole);
        
        rootCmd.AddOption(InCommand::OptionType::Variable, "log")
            .SetDescription("Set logging level")
            .SetDomain({"trace", "debug", "info", "warn", "error", "critical", "off"})
            .BindTo(logLevel);
        
        rootCmd.AddOption(InCommand::OptionType::Variable, "exitframecount")
            .SetDescription("Exit application after N frames (useful for automated testing)")
            .BindTo(exitFrameCount);
        
        // Enable auto-help (built-in --help/-h support)
        cmdParser.EnableAutoHelp("help", 'h', std::cout);
        
        // Parse the command line
        cmdParser.ParseArgs(argc, argv.data());
        
        // If help was requested, exit gracefully
        if (cmdParser.WasAutoHelpRequested())
        {
            // If attached to parent console, print newline so prompt appears on new line
            if (attachedToParent)
            {
                std::cout << std::endl;
            }
            return 0;
        }
        
        // Apply parsed options are now in noConsole and logLevel variables
    }
    catch (const InCommand::SyntaxException& e)
    {
        // Console is already open for error display
        std::cerr << "Command line error: " << e.GetMessage();
        if (!e.GetToken().empty())
        {
            std::cerr << " (token: '" << e.GetToken() << "')";
        }
        std::cerr << std::endl;
        // If attached to parent console, print newline for prompt
        if (attachedToParent)
        {
            std::cerr << std::endl;
        }
        return -1;
    }
    catch (const InCommand::ApiException& e)
    {
        std::cerr << "Internal error: " << e.GetMessage() << std::endl;
        // If attached to parent console, print newline for prompt
        if (attachedToParent)
        {
            std::cerr << std::endl;
        }
        return -1;
    }

    // If --no-console was specified and we're NOT attached to parent, close the console
    if (noConsole && !attachedToParent)
    {
        pConsole.reset();
    }

    // Map log level string to QLog::Level
    QLog::Level qlogLevel = QLog::Level::Info; // default
    if (logLevel == "trace")
        qlogLevel = QLog::Level::Trace;
    else if (logLevel == "debug")
        qlogLevel = QLog::Level::Debug;
    else if (logLevel == "info")
        qlogLevel = QLog::Level::Info;
    else if (logLevel == "warn")
        qlogLevel = QLog::Level::Warn;
    else if (logLevel == "error")
        qlogLevel = QLog::Level::Error;
    else if (logLevel == "critical")
        qlogLevel = QLog::Level::Critical;
    else if (logLevel == "off")
        qlogLevel = QLog::Level::Off;

    // Initialize logger with parsed log level
    CLogSink consoleSink;
    auto qlogLogger = std::make_unique<QLog::Logger>(consoleSink, qlogLevel);
    
    // Create QLogAdapter to wrap QLog logger for Canvas
    Gem::TGemPtr<Canvas::QLogAdapter> pAdapter;
    Gem::ThrowGemError(Gem::TGenericImpl<Canvas::QLogAdapter>::Create(&pAdapter, qlogLogger.release()));
    
    Gem::TGemPtr<Canvas::XLogger> pLogger;
    pAdapter->QueryInterface(&pLogger);

    // Register the application window class
    char szTitle[MAX_LOADSTRING];
    LoadStringA(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    ThinWin::CWindow::RegisterWindowClass(hInstance);

    // Create the application object
    std::unique_ptr<CApp> pApp(std::make_unique<CApp>(hInstance, szTitle, pLogger, exitFrameCount));

    // Initialize the application
    if (!pApp->Initialize (nCmdShow))
    {
        return FALSE;
    }

    return pApp->Execute();
}
