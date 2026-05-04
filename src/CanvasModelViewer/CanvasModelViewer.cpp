// CanvasModelViewer.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "CanvasModelViewer.h"
#include "CanvasCore.h"
#include "CanvasGfx.h"
#include "CanvasFbx.h"
#include "QLogAdapter.h"
#include "TokenParser.h"
#include "InCommand.h"
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <conio.h>

namespace
{

//------------------------------------------------------------------------------------------------
// WIC texture loader: decodes an FBX-imported texture (file path or embedded
// bytes) into an XGfxSurface and uploads pixels via the device copy queue.
// Returns false on any decode failure; caller logs a warning and proceeds
// without the texture.
//------------------------------------------------------------------------------------------------
class CWicTextureLoader
{
    CComPtr<IWICImagingFactory> m_pFactory;

    HRESULT EnsureFactory()
    {
        if (m_pFactory)
            return S_OK;
        return CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_pFactory));
    }

    HRESULT CreateMemoryStream(const std::vector<uint8_t>& bytes, IStream** ppStream)
    {
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
        if (!hMem)
            return E_OUTOFMEMORY;
        void* pDst = GlobalLock(hMem);
        if (!pDst)
        {
            GlobalFree(hMem);
            return E_FAIL;
        }
        memcpy(pDst, bytes.data(), bytes.size());
        GlobalUnlock(hMem);
        // Stream owns the HGLOBAL after success; on failure we free it.
        HRESULT hr = CreateStreamOnHGlobal(hMem, TRUE, ppStream);
        if (FAILED(hr))
            GlobalFree(hMem);
        return hr;
    }

public:
    bool Load(
        Canvas::XGfxDevice* pDevice,
        const Canvas::Fbx::ImportedTextureRef& ref,
        Gem::TGemPtr<Canvas::XGfxSurface>& outSurface,
        Canvas::XLogger* pLogger)
    {
        outSurface = nullptr;

        HRESULT hr = EnsureFactory();
        if (FAILED(hr))
        {
            Canvas::LogError(pLogger, "WIC: failed to create imaging factory hr=0x%08X",
                static_cast<unsigned>(hr));
            return false;
        }

        CComPtr<IWICBitmapDecoder> pDecoder;
        if (ref.Embedded && !ref.EmbeddedBytes.empty())
        {
            CComPtr<IStream> pStream;
            hr = CreateMemoryStream(ref.EmbeddedBytes, &pStream);
            if (FAILED(hr))
            {
                Canvas::LogWarn(pLogger, "WIC: embedded stream failed hr=0x%08X for '%s'",
                    static_cast<unsigned>(hr), ref.AbsoluteFilePath.c_str());
                return false;
            }
            hr = m_pFactory->CreateDecoderFromStream(pStream, nullptr,
                WICDecodeMetadataCacheOnDemand, &pDecoder);
        }
        else
        {
            std::wstring widePath(ref.AbsoluteFilePath.begin(), ref.AbsoluteFilePath.end());
            hr = m_pFactory->CreateDecoderFromFilename(widePath.c_str(), nullptr,
                GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
        }
        if (FAILED(hr))
        {
            Canvas::LogWarn(pLogger, "WIC: decoder creation failed hr=0x%08X for '%s'",
                static_cast<unsigned>(hr), ref.AbsoluteFilePath.c_str());
            return false;
        }

        CComPtr<IWICBitmapFrameDecode> pFrame;
        hr = pDecoder->GetFrame(0, &pFrame);
        if (FAILED(hr))
            return false;

        CComPtr<IWICFormatConverter> pConverter;
        hr = m_pFactory->CreateFormatConverter(&pConverter);
        if (FAILED(hr))
            return false;
        hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
        {
            Canvas::LogWarn(pLogger, "WIC: format converter init failed hr=0x%08X for '%s'",
                static_cast<unsigned>(hr), ref.AbsoluteFilePath.c_str());
            return false;
        }

        UINT width = 0, height = 0;
        hr = pConverter->GetSize(&width, &height);
        if (FAILED(hr) || width == 0 || height == 0)
            return false;

        const UINT rowPitch = width * 4;
        std::vector<uint8_t> pixels(static_cast<size_t>(rowPitch) * height);
        hr = pConverter->CopyPixels(nullptr, rowPitch,
            static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr))
            return false;

        Gem::TGemPtr<Canvas::XGfxSurface> pSurface;
        Canvas::GfxSurfaceDesc desc = Canvas::GfxSurfaceDesc::SurfaceDesc2D(
            Canvas::GfxFormat::R8G8B8A8_UNorm, width, height,
            Canvas::SurfaceFlag_ShaderResource);
        Gem::Result r = pDevice->CreateSurface(desc, &pSurface);
        if (Gem::Failed(r))
        {
            Canvas::LogWarn(pLogger, "WIC: CreateSurface failed for '%s'",
                ref.AbsoluteFilePath.c_str());
            return false;
        }

        r = pDevice->UploadTextureRegion(pSurface.Get(), 0, 0, width, height,
            pixels.data(), rowPitch);
        if (Gem::Failed(r))
        {
            Canvas::LogWarn(pLogger, "WIC: UploadTextureRegion failed for '%s'",
                ref.AbsoluteFilePath.c_str());
            return false;
        }

        outSurface = std::move(pSurface);
        return true;
    }
};

} // anonymous namespace

//------------------------------------------------------------------------------------------------
// D3D12 Agility SDK version exports - required to activate newer D3D12 APIs
// These should be exported from the main executable for best compatibility
//------------------------------------------------------------------------------------------------
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 616; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

//------------------------------------------------------------------------------------------------
// CConsole — allocates a dedicated console window for live log output.
// Only created when the "log --console" option is specified.  Never
// attaches to the parent terminal, avoiding the console-mode corruption
// that plagued earlier versions.
//------------------------------------------------------------------------------------------------
class CConsole
{
    FILE* m_stdout;
    FILE* m_stderr;

public:
    CConsole()
        : m_stdout(nullptr)
        , m_stderr(nullptr)
    {
        AllocConsole();
        SetConsoleTitleA("CanvasModelViewer - Log");
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

//------------------------------------------------------------------------------------------------
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
        oss << QLog::FormatTimestamp(message) << QLog::ToString(message.level) << ": " << message.text << '\n';
        std::string formatted = oss.str();

        OutputDebugStringA(formatted.c_str());

        if (m_logFile.is_open())
        {
            m_logFile << formatted;
            m_logFile.flush();
        }

        if (m_consoleOutput)
        {
            fputs(formatted.c_str(), stdout);
        }
    }
};

//------------------------------------------------------------------------------------------------
// Window subclass providing message-driven mouse input for the viewer.
// Deltas are accumulated from WM_INPUT (raw input) so they are not
// clamped by ClipCursor, giving unbounded rotation.  ClipCursor keeps
// the hidden cursor inside the client area to maintain window focus.
// SetCursorPos is intentionally avoided — cursor-warp latency over
// Remote Desktop causes large spurious deltas.
//------------------------------------------------------------------------------------------------
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

            // Confine the hidden cursor to the client area so it can't
            // wander to another window/monitor and trigger a capture release.
            RECT rc;
            ::GetClientRect(m_hWnd, &rc);
            ::MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<POINT*>(&rc), 2);
            ::ClipCursor(&rc);

            // Register for raw mouse input.  Raw deltas bypass ClipCursor
            // clamping, so rotation is unbounded.  Over RDP this also
            // triggers the relative-mouse-mode channel (Win 10 1709+).
            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
            rid.usUsage     = 0x02; // HID_USAGE_GENERIC_MOUSE
            rid.dwFlags     = 0;
            rid.hwndTarget  = m_hWnd;
            ::RegisterRawInputDevices(&rid, 1, sizeof(rid));

            m_Captured = true;
            m_HasLastAbsPos = false;
            m_MouseDX = 0.0f;
            m_MouseDY = 0.0f;
        }
        else if (!captured && m_Captured)
        {
            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01;
            rid.usUsage     = 0x02;
            rid.dwFlags     = RIDEV_REMOVE;
            rid.hwndTarget  = nullptr;
            ::RegisterRawInputDevices(&rid, 1, sizeof(rid));

            ::ClipCursor(nullptr);
            ::ReleaseCapture();
            ::SetCursor(LoadCursor(NULL, IDC_ARROW));
            m_Captured = false;
        }
    }

    void ConsumeMouseDelta(float& dx, float& dy)
    {
        dx = m_MouseDX;
        dy = m_MouseDY;
        m_MouseDX = 0.0f;
        m_MouseDY = 0.0f;
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
                            // Absolute coordinates (RDP absolute mode, touch, etc.)
                            // Map the 0-65535 range to virtual-desktop pixels.
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
                            // Relative movement (local mouse, RDP relative mode).
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
                // Keep cursor hidden (WM_SETCURSOR is suppressed during capture).
                ::SetCursor(NULL);
                return 0;
            }
            break;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                // Hide cursor while captured; restore arrow otherwise
                // (the window class has no default cursor).
                ::SetCursor(m_Captured ? NULL : LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lParam) != m_hWnd)
                m_Captured = false;
            return 0;
        }

        return CWindow::WindowProc(uMsg, wParam, lParam);
    }

private:
    float m_MouseDX = 0.0f;
    float m_MouseDY = 0.0f;
    float m_LastAbsX = 0.0f;
    float m_LastAbsY = 0.0f;
    bool m_HasLastAbsPos = false;
    bool m_Captured = false;
};

//------------------------------------------------------------------------------------------------
class CApp
{
    std::string m_Title;
    HINSTANCE m_hInstance;
    Gem::TGemPtr<Canvas::XLogger> m_pLogger;
    std::unique_ptr<CViewerWindow> m_pWindow;
    Gem::TGemPtr<Canvas::XCanvas> m_pCanvas;
    Gem::TGemPtr<Canvas::XSceneGraph> m_pScene;
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
    Gem::TGemPtr<Canvas::XUIRectElement> m_pHudPanel;
    Gem::TGemPtr<Canvas::XUITextElement> m_pTitleText;
    Gem::TGemPtr<Canvas::XUITextElement> m_pFpsText;
    int m_exitFrameCount;  // -1 means don't exit automatically; >= 0 means exit after N frames
    bool m_logFps;
    float m_fps = 0.0f;
    std::string m_fpsString;
    std::filesystem::path m_ModelPath;

    Gem::TGemPtr<Canvas::XModel> m_pModel;
    Gem::TGemPtr<Canvas::XSceneGraphNode> m_pInstanceRoot;

    // Camera controller state
    float m_CameraYaw = 0.0f;    // Radians, around world Z (up)
    float m_CameraPitch = 0.0f;  // Radians, around camera right

    void FrameCameraToBounds(
        Canvas::XCamera *pCamera,
        Canvas::XSceneGraphNode *pCameraNode,
        const Canvas::Math::AABB &bounds)
    {
        if (!pCameraNode || !bounds.IsValid())
            return;

        const Canvas::Math::FloatVector4 sceneCenter = bounds.GetCenter();
        const Canvas::Math::FloatVector4 extents = bounds.GetExtents();
        const float radius = std::max(0.5f, sqrtf(extents[0] * extents[0] + extents[1] * extents[1] + extents[2] * extents[2]));

        float fov = static_cast<float>(Canvas::Math::Pi / 4.0);
        if (pCamera)
            fov = std::clamp(pCamera->GetFovAngle(), 0.2f, 2.8f);

        const float distance = std::max(2.0f, (radius / tanf(fov * 0.5f)) * 1.35f);
        const Canvas::Math::FloatVector4 viewDir = Canvas::Math::FloatVector4(0.55f, 0.55f, 0.35f, 0.0f).Normalize();
        const Canvas::Math::FloatVector4 cameraPosition = sceneCenter - viewDir * distance;
        const Canvas::Math::FloatVector4 basisForward = (sceneCenter - cameraPosition).Normalize();
        const Canvas::Math::FloatVector4 worldUp(0.0f, 0.0f, 1.0f, 0.0f);

        Canvas::Math::FloatMatrix4x4 rotationMatrix = Canvas::Math::IdentityMatrix<float, 4, 4>();
        rotationMatrix[0] = basisForward;
        Canvas::Math::ComposePointToBasisVectors(worldUp, basisForward, rotationMatrix[1], rotationMatrix[2]);

        pCameraNode->SetLocalTranslation(cameraPosition);
        pCameraNode->SetLocalRotation(Canvas::Math::QuaternionFromRotationMatrix(rotationMatrix));

        if (pCamera)
        {
            const float nearClip = std::max(0.05f, distance - radius * 2.0f);
            const float farClip = std::max(nearClip + 100.0f, distance + radius * 4.0f);
            pCamera->SetNearClip(nearClip);
            pCamera->SetFarClip(farClip);
        }

        m_CameraYaw = atan2f(basisForward[1], basisForward[0]);
        m_CameraPitch = asinf(std::clamp(basisForward[2], -1.0f, 1.0f));
    }

    void LogCameraTransform(const char *label, Canvas::XCamera *pCamera)
    {
        if (!pCamera)
        {
            Canvas::LogWarn(m_pLogger.Get(), "%s: camera is null", label);
            return;
        }

        Canvas::XSceneGraphNode *pNode = pCamera->GetAttachedNode();
        if (!pNode)
        {
            Canvas::LogWarn(m_pLogger.Get(), "%s: camera '%s' is not attached to a node", label, pCamera->GetName());
            return;
        }

        const Canvas::Math::FloatMatrix4x4 world = pNode->GetGlobalMatrix();
        const Canvas::Math::FloatVector4 position(world[3][0], world[3][1], world[3][2], 0.0f);
        const Canvas::Math::FloatVector4 forward(world[0][0], world[0][1], world[0][2], 0.0f);
        const Canvas::Math::FloatVector4 left(world[1][0], world[1][1], world[1][2], 0.0f);
        const Canvas::Math::FloatVector4 up(world[2][0], world[2][1], world[2][2], 0.0f);
        const float nearClip = pCamera->GetNearClip();
        const float farClip = pCamera->GetFarClip();
        const float fovAngle = pCamera->GetFovAngle();
        const float aspectRatio = pCamera->GetAspectRatio();

        Canvas::LogInfo(m_pLogger.Get(),
            "%s: camera='%s' pos=(%.3f, %.3f, %.3f) forward=(%.3f, %.3f, %.3f) left=(%.3f, %.3f, %.3f) up=(%.3f, %.3f, %.3f) near=%.3f far=%.3f fov=%.3f aspect=%.3f",
            label,
            pCamera->GetName(),
            position[0], position[1], position[2],
            forward[0], forward[1], forward[2],
            left[0], left[1], left[2],
            up[0], up[1], up[2],
            nearClip, farClip, fovAngle, aspectRatio);
    }

    void LogSceneBounds(const char *label, const Canvas::Math::AABB &bounds)
    {
        if (!bounds.IsValid())
        {
            Canvas::LogWarn(m_pLogger.Get(), "%s: scene bounds invalid", label);
            return;
        }

        const Canvas::Math::FloatVector4 minPoint = bounds.Min;
        const Canvas::Math::FloatVector4 maxPoint = bounds.Max;
        const Canvas::Math::FloatVector4 center = bounds.GetCenter();
        const Canvas::Math::FloatVector4 extents = bounds.GetExtents();
        const float radius = sqrtf(extents[0] * extents[0] + extents[1] * extents[1] + extents[2] * extents[2]);

        Canvas::LogInfo(m_pLogger.Get(),
            "%s: min=(%.3f, %.3f, %.3f) max=(%.3f, %.3f, %.3f) center=(%.3f, %.3f, %.3f) extents=(%.3f, %.3f, %.3f) radius=%.3f",
            label,
            minPoint[0], minPoint[1], minPoint[2],
            maxPoint[0], maxPoint[1], maxPoint[2],
            center[0], center[1], center[2],
            extents[0], extents[1], extents[2],
            radius);
    }

    void LogNodeTransform(const char *label, Canvas::XSceneGraphNode *pNode)
    {
        if (!pNode)
        {
            Canvas::LogWarn(m_pLogger.Get(), "%s: node is null", label);
            return;
        }

        const Canvas::Math::FloatVector4 localTranslation = pNode->GetLocalTranslation();
        const Canvas::Math::FloatVector4 localScale = pNode->GetLocalScale();
        const Canvas::Math::FloatMatrix4x4 world = pNode->GetGlobalMatrix();
        const Canvas::Math::FloatVector4 worldPosition(world[3][0], world[3][1], world[3][2], 0.0f);
        const float basisXLen = sqrtf(world[0][0] * world[0][0] + world[0][1] * world[0][1] + world[0][2] * world[0][2]);
        const float basisYLen = sqrtf(world[1][0] * world[1][0] + world[1][1] * world[1][1] + world[1][2] * world[1][2]);
        const float basisZLen = sqrtf(world[2][0] * world[2][0] + world[2][1] * world[2][1] + world[2][2] * world[2][2]);

        Canvas::LogInfo(m_pLogger.Get(),
            "%s: node='%s' localT=(%.3f, %.3f, %.3f) localS=(%.3f, %.3f, %.3f) worldPos=(%.3f, %.3f, %.3f) worldScale=(%.3f, %.3f, %.3f)",
            label,
            pNode->GetName(),
            localTranslation[0], localTranslation[1], localTranslation[2],
            localScale[0], localScale[1], localScale[2],
            worldPosition[0], worldPosition[1], worldPosition[2],
            basisXLen, basisYLen, basisZLen);
    }

    void LogCameraAffineMatrix(const char *label, Canvas::XCamera *pCamera)
    {
        if (!pCamera)
            return;

        Canvas::XSceneGraphNode *pNode = pCamera->GetAttachedNode();
        if (!pNode)
            return;

        const Canvas::Math::FloatMatrix4x4 world = pNode->GetGlobalMatrix();
        Canvas::LogInfo(m_pLogger.Get(),
            "%s: [[%.6f %.6f %.6f][%.6f %.6f %.6f][%.6f %.6f %.6f][%.6f %.6f %.6f]]",
            label,
            world[0][0], world[0][1], world[0][2],
            world[1][0], world[1][1], world[1][2],
            world[2][0], world[2][1], world[2][2],
            world[3][0], world[3][1], world[3][2]);
    }



    bool TryLoadImportedScene(
        Canvas::XCanvas *pCanvas,
        Canvas::XSceneGraph *pScene,
        Canvas::XCamera *pDefaultCamera,
        Canvas::XSceneGraphNode *pDefaultCameraNode)
    {
        Canvas::XGfxDevice *pDevice = pScene->GetDevice();
        try
        {
        if (m_ModelPath.empty())
        {
            Canvas::LogError(m_pLogger.Get(), "No --fbx path provided; CanvasModelViewer requires a model path");
            return false;
        }

        const std::filesystem::path requestedPath = m_ModelPath;
        std::error_code pathEc;
        const std::filesystem::path resolvedPath = std::filesystem::absolute(requestedPath, pathEc);
        const std::filesystem::path importPath = pathEc ? requestedPath : resolvedPath;

        std::error_code cwdEc;
        const std::filesystem::path cwdPath = std::filesystem::current_path(cwdEc);
        const std::string cwdString = cwdEc ? std::string("<unavailable>") : cwdPath.string();

        Canvas::LogInfo(m_pLogger.Get(), "FBX request path: '%s'", requestedPath.string().c_str());
        Canvas::LogInfo(m_pLogger.Get(), "FBX resolved path: '%s'", importPath.string().c_str());
        Canvas::LogInfo(m_pLogger.Get(), "Process working directory: '%s'", cwdString.c_str());

        std::error_code existsEc;
        if (!std::filesystem::exists(importPath, existsEc))
        {
            Canvas::LogError(m_pLogger.Get(), "FBX file not found: '%s'", importPath.string().c_str());
            return false;
        }

        Canvas::Fbx::ImportOptions options;
        Canvas::Fbx::ImportedScene imported;

        const std::wstring widePath = importPath.wstring();
        Canvas::LogInfo(m_pLogger.Get(), "Calling FBX importer...");
        const HRESULT hr = Canvas::Fbx::ImportScene(widePath.c_str(), options, &imported);
        Canvas::LogInfo(m_pLogger.Get(), "FBX importer returned hr=0x%08X", static_cast<unsigned int>(hr));
        Canvas::LogInfo(m_pLogger.Get(), "FBX import counts: meshes=%zu lights=%zu nodes=%zu diagnostics=%zu",
            imported.Meshes.size(), imported.Lights.size(), imported.Nodes.size(), imported.Diagnostics.size());

        for (const Canvas::Fbx::ImportDiag &diag : imported.Diagnostics)
        {
            if (diag.Level == Canvas::Fbx::DiagLevel::Error)
                Canvas::LogError(m_pLogger.Get(), "FBX: %s", diag.Message.c_str());
            else if (diag.Level == Canvas::Fbx::DiagLevel::Warning)
                Canvas::LogWarn(m_pLogger.Get(), "FBX: %s", diag.Message.c_str());
            else
                Canvas::LogInfo(m_pLogger.Get(), "FBX: %s", diag.Message.c_str());
        }

        bool hasImportErrors = FAILED(hr);
        for (const Canvas::Fbx::ImportDiag &diag : imported.Diagnostics)
        {
            if (diag.Level == Canvas::Fbx::DiagLevel::Error)
            {
                hasImportErrors = true;
                break;
            }
        }

        if (hasImportErrors || imported.Meshes.empty())
        {
            Canvas::LogError(m_pLogger.Get(), "FBX import failed or produced no meshes: %s", m_ModelPath.string().c_str());
            return false;
        }

        LogSceneBounds("Imported scene bounds", imported.SceneBounds);

        // -----------------------------------------------------------------
        // Build XModel from imported FBX data
        // -----------------------------------------------------------------
        CWicTextureLoader textureLoader;
        Canvas::XLogger *pLocalLogger = m_pLogger.Get();
        Canvas::Fbx::BuildModelOptions buildOpts;
        buildOpts.pModelName = importPath.filename().string().c_str();
        buildOpts.pLogger = pLocalLogger;
        buildOpts.TextureLoader = [&textureLoader, pLocalLogger](
            Canvas::XGfxDevice *pDev,
            const Canvas::Fbx::ImportedTextureRef &ref,
            Canvas::XGfxSurface **ppSurface) -> bool
        {
            Gem::TGemPtr<Canvas::XGfxSurface> surface;
            if (!textureLoader.Load(pDev, ref, surface, pLocalLogger))
                return false;
            *ppSurface = surface.Detach();
            return true;
        };

        Gem::TGemPtr<Canvas::XModel> pModel;
        Gem::ThrowGemError(Canvas::Fbx::BuildModel(pCanvas, pDevice, imported, buildOpts, &pModel));

        // Store model bounds
        // (SetBounds is on CModel, not the interface — we set it here since
        // the model doesn't compute bounds automatically yet)

        // -----------------------------------------------------------------
        // Instantiate model into the scene
        // -----------------------------------------------------------------
        Canvas::ModelInstantiateResult result{};
        Gem::ThrowGemError(pModel->Instantiate(pScene->GetRootSceneGraphNode(), &result));

        m_pModel = pModel;
        m_pInstanceRoot = result.pInstanceRoot;

        // -----------------------------------------------------------------
        // Camera selection from instantiate result
        // -----------------------------------------------------------------
        Canvas::XCamera *pSelectedCamera = result.pActiveCamera;

        if (pSelectedCamera)
        {
            pScene->SetActiveCamera(pSelectedCamera);
            m_pCamera = pSelectedCamera;
            LogCameraTransform("Imported active camera", pSelectedCamera);
            LogCameraAffineMatrix("Imported camera affine", pSelectedCamera);

            // Keep mouse-look deltas aligned with imported camera orientation.
            Canvas::XSceneGraphNode *pActiveNode = pSelectedCamera->GetAttachedNode();
            if (pActiveNode)
            {
                const Canvas::Math::FloatMatrix4x4 world = pActiveNode->GetGlobalMatrix();
                const Canvas::Math::FloatVector4 forward(world[0][0], world[0][1], world[0][2], 0.0f);
                m_CameraYaw = atan2f(forward[1], forward[0]);
                m_CameraPitch = asinf(std::clamp(forward[2], -1.0f, 1.0f));
            }
            else
            {
                Canvas::LogWarn(m_pLogger.Get(), "Selected imported camera is not attached to a node; using default viewer camera framing");
                if (pDefaultCamera && pDefaultCameraNode)
                {
                    pScene->SetActiveCamera(pDefaultCamera);
                    m_pCamera = pDefaultCamera;
                    FrameCameraToBounds(pDefaultCamera, pDefaultCameraNode, imported.SceneBounds);
                    LogCameraTransform("Fallback default camera", pDefaultCamera);
                    LogCameraAffineMatrix("Fallback default camera affine", pDefaultCamera);
                }
                else
                {
                    Canvas::LogError(m_pLogger.Get(), "No usable attached camera available after import");
                    return false;
                }
            }
        }
        else if (pDefaultCamera && pDefaultCameraNode)
        {
            pScene->SetActiveCamera(pDefaultCamera);
            m_pCamera = pDefaultCamera;
            FrameCameraToBounds(pDefaultCamera, pDefaultCameraNode, imported.SceneBounds);
            LogCameraTransform("Fallback default camera", pDefaultCamera);
            LogCameraAffineMatrix("Fallback default camera affine", pDefaultCamera);
            Canvas::LogWarn(m_pLogger.Get(), "Imported scene has no cameras; using default viewer camera framing");
        }
        else
        {
            Canvas::LogError(m_pLogger.Get(), "No usable camera available after import");
            return false;
        }
        Canvas::LogInfo(m_pLogger.Get(), "Loaded FBX scene '%s' (%zu meshes, %zu lights, %zu cameras, %zu nodes)",
            m_ModelPath.string().c_str(),
            imported.Meshes.size(),
            imported.Lights.size(),
            imported.Cameras.size(),
            imported.Nodes.size());
        return true;
        }
        catch (const std::bad_alloc&)
        {
            Canvas::LogError(m_pLogger.Get(), "Out of memory while importing/building FBX scene '%s'", m_ModelPath.string().c_str());
            return false;
        }
        catch (const Gem::GemError& e)
        {
            Canvas::LogError(m_pLogger.Get(), "Gem error while importing/building FBX scene '%s': %s",
                m_ModelPath.string().c_str(),
                GemResultString(e.Result()));
            return false;
        }
        catch (const std::exception& e)
        {
            Canvas::LogError(m_pLogger.Get(), "Exception while importing/building FBX scene '%s': %s",
                m_ModelPath.string().c_str(),
                e.what());
            return false;
        }
    }

public:
    CApp(
        HINSTANCE hInstance,
        PCSTR szTitle,
        Gem::TGemPtr<Canvas::XLogger> pLogger,
        int exitFrameCount = -1,
        bool logFps = false,
        std::filesystem::path modelPath = {}) :
        m_pLogger(pLogger),
        m_Title(szTitle),
        m_hInstance(hInstance),
        m_exitFrameCount(exitFrameCount),
        m_logFps(logFps),
        m_ModelPath(std::move(modelPath))
        {
        }

    ~CApp() 
    {
    };

    bool Initialize(int nCmdShow)
    {
        Canvas::CFunctionSentinel sentinel("CApp::Initialize", m_pLogger.Get());
        const char* initStep = "startup";

        // Construct the CWindow
        try
        {
            initStep = "create_window";
            auto pWindow = std::make_unique<CViewerWindow>(m_Title.c_str(), m_hInstance, WS_OVERLAPPEDWINDOW);
            if (!pWindow.get())
            {
                Gem::ThrowGemError(Gem::Result::OutOfMemory);
            }

            pWindow->ShowWindow(nCmdShow);
            pWindow->UpdateWindow();

            initStep = "create_canvas";
            Gem::TGemPtr<Canvas::XCanvas> pCanvas;
            Gem::ThrowGemError(Canvas::CreateCanvas(m_pLogger.Get(), &pCanvas));

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

            initStep = "load_graphics_plugin";
            // Load the graphics plugin
            Gem::TGemPtr<Canvas::XCanvasPlugin> pGfxPlugin;
            Gem::ThrowGemError(pCanvas->LoadPlugin("CanvasGfx12.dll", &pGfxPlugin));

            initStep = "create_device";
            // Create the graphics device
            Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
            Gem::ThrowGemError(pGfxPlugin->CreateCanvasElement(
                pCanvas,
                Canvas::TypeId::TypeId_GfxDevice,
                "MainDevice",
                Canvas::XGfxDevice::IId,
                (void**)&pDevice));

            initStep = "create_scene";
            Gem::TGemPtr<Canvas::XSceneGraph> pScene;
            Gem::ThrowGemError(pCanvas->CreateSceneGraph(pDevice, &pScene, "MainScene"));

            initStep = "create_render_queue";
            // Create the render queue
            Gem::TGemPtr<Canvas::XGfxRenderQueue> pGfxRenderQueue;
            Gem::ThrowGemError(pDevice->CreateRenderQueue(&pGfxRenderQueue));

            initStep = "create_swap_chain";
            // Create the swapchain
            Gem::TGemPtr<Canvas::XGfxSwapChain> pSwapChain;
            Gem::ThrowGemError(pGfxRenderQueue->CreateSwapChain(pWindow->m_hWnd, true, &pSwapChain, Canvas::GfxFormat::R16G16B16A16_Float, 4));

            initStep = "create_camera";
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
            Canvas::Math::FloatVector4 cameraPosition(7.0f, -7.0f, 5.0f, 0.0f);
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
            LogCameraAffineMatrix("Default camera created affine", pCamera);

            // Initialize camera yaw/pitch from the look direction
            m_CameraYaw = atan2f(basisForward[1], basisForward[0]);
            m_CameraPitch = asinf(std::clamp(basisForward[2], -1.0f, 1.0f));
            
            pScene->GetRootSceneGraphNode()->AddChild(pCameraNode);
            pScene->SetActiveCamera(pCamera);

            initStep = "load_or_build_scene";
            if (!TryLoadImportedScene(pCanvas, pScene, pCamera, pCameraNode))
            {
                Canvas::LogError(m_pLogger.Get(), "Failed to load model scene; initialization aborted");
                return false;
            }

            initStep = "load_fonts";
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

            initStep = "create_ui_graph";
            // Create UI graph for text rendering
            Gem::TGemPtr<Canvas::XUIGraph> pUIGraph;
            Gem::ThrowGemError(pCanvas->CreateUIGraph(pDevice, &pUIGraph));

            // Single HUD node for all UI elements
            Gem::TGemPtr<Canvas::XUIGraphNode> pHudNode;
            Gem::ThrowGemError(pUIGraph->CreateNode(nullptr, &pHudNode));
            pHudNode->SetLocalPosition(Canvas::Math::FloatVector2(6.0f, 6.0f));

            // HUD background panel
            Gem::TGemPtr<Canvas::XUIRectElement> pHudPanel;
            Gem::ThrowGemError(pDevice->CreateRectElement(&pHudPanel));
            pHudNode->BindElement(pHudPanel);
            pHudPanel->SetSize(Canvas::Math::FloatVector2(340.0f, 70.0f));
            pHudPanel->SetFillColor(Canvas::Math::FloatVector4(0.125f, 0.125f, 0.125f, 0.75f));

            // Title text
            Gem::TGemPtr<Canvas::XUITextElement> pTitleText;
            Gem::ThrowGemError(pDevice->CreateTextElement(&pTitleText));
            pHudNode->BindElement(pTitleText);
            pTitleText->SetLocalOffset(Canvas::Math::FloatVector2(4.0f, 4.0f));
            pTitleText->SetFont(pFont);
            {
                Canvas::TextLayoutConfig titleConfig;
                titleConfig.FontSize = 32.0f;
                titleConfig.Color = Canvas::Math::FloatVector4(1.0f, 1.0f, 1.0f, 1.0f);
                pTitleText->SetLayoutConfig(titleConfig);
            }
            pTitleText->SetText("Canvas Model Viewer");

            // FPS text
            Gem::TGemPtr<Canvas::XUITextElement> pFpsText;
            Gem::ThrowGemError(pDevice->CreateTextElement(&pFpsText));
            pHudNode->BindElement(pFpsText);
            pFpsText->SetLocalOffset(Canvas::Math::FloatVector2(4.0f, 40.0f));
            pFpsText->SetFont(pFontMono);
            {
                Canvas::TextLayoutConfig monoConfig;
                monoConfig.FontSize = 18.0f;
                monoConfig.Color = Canvas::Math::FloatVector4(0.8f, 0.8f, 0.8f, 1.0f);
                pFpsText->SetLayoutConfig(monoConfig);
            }
            pFpsText->SetText("FPS: --");

            initStep = "finalize_members";
            m_pGfxPlugin.Attach(pGfxPlugin.Detach());
            m_pGfxDevice.Attach(pDevice.Detach());
            m_pGfxRenderQueue.Attach(pGfxRenderQueue.Detach());
            m_pGfxSwapChain.Attach(pSwapChain.Detach());
            if (!m_pCamera.Get())
                m_pCamera.Attach(pCamera.Detach());
            m_pCanvas.Attach(pCanvas.Detach());
            m_pScene.Attach(pScene.Detach());
            m_pFont.Attach(pFont.Detach());
            m_pFontMono.Attach(pFontMono.Detach());
            m_pUIGraph.Attach(pUIGraph.Detach());
            m_pHudPanel.Attach(pHudPanel.Detach());
            m_pTitleText.Attach(pTitleText.Detach());
            m_pFpsText.Attach(pFpsText.Detach());

            m_pWindow = std::move(pWindow);

            return true;
        }
        catch (std::bad_alloc &)
        {
            Canvas::LogError(m_pLogger.Get(), "Initialize failed at step '%s' with std::bad_alloc", initStep);
            sentinel.SetResultCode(Gem::Result::OutOfMemory);
            return false;
        }
        catch (Gem::GemError &e)
        {
            Canvas::LogError(m_pLogger.Get(), "Initialize failed at step '%s' with Gem::Result=%s",
                initStep,
                GemResultString(e.Result()));
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

        // RAII guard to release mouse capture on exit (normal or exceptional).
        struct CursorGuard
        {
            CViewerWindow* pWindow;
            CursorGuard(CViewerWindow* w) : pWindow(w) {}
            ~CursorGuard() { pWindow->SetMouseCaptured(false); }
        } cursorGuard(m_pWindow.get());

        // Capture mouse on startup
        m_pWindow->SetMouseCaptured(true);

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
                if (m_logFps) Canvas::LogInfo(m_pLogger.Get(), "FPS: %.1f", m_fps);
                fpsTime = currTime;
                fpsCounter = 0;
            }
            ++fpsCounter;
            ++frameCount;

            //==================================================================
            // Camera controller: mouse look + WASD movement
            //==================================================================
            
            // Track focus changes — release/acquire mouse capture as needed
            HWND foreground = GetForegroundWindow();
            HWND foregroundRoot = foreground ? GetAncestor(foreground, GA_ROOT) : nullptr;
            bool hasFocus = (foreground == m_pWindow->m_hWnd) ||
                            (foregroundRoot == m_pWindow->m_hWnd) ||
                            (foreground && IsChild(m_pWindow->m_hWnd, foreground));
            if (hasFocus && !m_pWindow->IsMouseCaptured())
            {
                m_pWindow->SetMouseCaptured(true);
            }
            else if (!hasFocus && m_pWindow->IsMouseCaptured())
            {
                m_pWindow->SetMouseCaptured(false);
            }

            float dx = 0.0f;
            float dy = 0.0f;
            bool rotated = false;
            if (m_pWindow->IsMouseCaptured())
            {
                m_pWindow->ConsumeMouseDelta(dx, dy);

                if (fabsf(dx) > 0.0f || fabsf(dy) > 0.0f)
                {
                    m_CameraYaw   -= dx * kMouseSensitivity;
                    m_CameraPitch -= dy * kMouseSensitivity;
                    m_CameraPitch = std::clamp(m_CameraPitch, -kPitchLimit, kPitchLimit);
                    rotated = true;
                }
            }

            // Build camera orientation matrix from yaw/pitch (Z-up world)
            // Canvas row-vector convention: row 0 = forward, row 1 = right, row 2 = up
            float cy = cosf(m_CameraYaw), sy = sinf(m_CameraYaw);
            float cp = cosf(m_CameraPitch), sp = sinf(m_CameraPitch);

            Canvas::Math::FloatVector4 forward(cy * cp, sy * cp, sp, 0.0f);
            Canvas::Math::FloatVector4 worldUp(0.0f, 0.0f, 1.0f, 0.0f);

            Canvas::Math::FloatMatrix4x4 rotMat = Canvas::Math::IdentityMatrix<float, 4, 4>();
            rotMat[0] = forward;
            Canvas::Math::ComposePointToBasisVectors(worldUp, forward, rotMat[1], rotMat[2]);
            Canvas::Math::FloatQuaternion cameraQuat = Canvas::Math::QuaternionFromRotationMatrix(rotMat);

            // Right vector for WASD strafing (from the rotation matrix)
            Canvas::Math::FloatVector4 right = rotMat[1];

            if (hasFocus)
            {
                // WASD movement in camera-local space (works regardless of mouse capture state).
                Canvas::Math::FloatVector4 moveDir(0.0f, 0.0f, 0.0f, 0.0f);
                if (GetAsyncKeyState('W') & 0x8000) moveDir = moveDir + forward;
                if (GetAsyncKeyState('S') & 0x8000) moveDir = moveDir - forward;
                if (GetAsyncKeyState('D') & 0x8000) moveDir = moveDir - right;
                if (GetAsyncKeyState('A') & 0x8000) moveDir = moveDir + right;
                if (GetAsyncKeyState(VK_SPACE) & 0x8000) moveDir = moveDir + Canvas::Math::FloatVector4(0, 0, 1, 0);
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) moveDir = moveDir + Canvas::Math::FloatVector4(0, 0, 1, 0);
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) moveDir = moveDir - Canvas::Math::FloatVector4(0, 0, 1, 0);
                if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { running = false; break; }

                float moveMag = sqrtf(Canvas::Math::DotProduct(moveDir, moveDir));
                bool moved = false;
                if (moveMag > 0.001f)
                {
                    moveDir = moveDir * (1.0f / moveMag);
                    moved = true;
                }

                auto *pCamNode = m_pCamera->GetAttachedNode();
                if (pCamNode && (rotated || moved))
                {
                    auto pos = pCamNode->GetLocalTranslation();
                    if (moved)
                        pos = pos + moveDir * (kMoveSpeed * dtime);
                    pCamNode->SetLocalTranslation(pos);
                    pCamNode->SetLocalRotation(cameraQuat);
                    LogCameraAffineMatrix("Camera moved affine", m_pCamera);
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
            m_pUIGraph->SubmitRenderables(m_pGfxRenderQueue);

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

    // COM initialization for WIC (texture decode) and other shell APIs.
    // STA on the UI thread is the conventional choice for Win32 GUI apps.
    // Tolerate a pre-existing apartment selection to stay friendly to anything
    // that might have CoInitialize'd ahead of us.
    HRESULT comInitHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comOwned = SUCCEEDED(comInitHr);

    auto comCleanup = [comOwned]()
    {
        if (comOwned)
            CoUninitialize();
    };

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

    std::string logLevel = "warn";
    std::string logFile;
    bool logConsole = false;
    bool logFps = false;
    std::string fbxPath;
    int exitFrameCount = -1;  // -1 means don't exit automatically

    try
    {
        // Setup command line parser
        InCommand::CommandParser cmdParser("CanvasModelViewer");
        auto& rootCmd = cmdParser.GetAppCommandDecl();
        rootCmd.SetDescription("Canvas 3D Model Viewer");

        rootCmd.AddOption(InCommand::OptionType::Variable, "exitframecount")
            .SetDescription("Exit application after N frames (useful for automated testing)")
            .BindTo(exitFrameCount);

        rootCmd.AddOption(InCommand::OptionType::Variable, "fbx")
            .SetDescription("Path to an FBX file to import at startup")
            .BindTo(fbxPath);

        // "log" subcommand — all logging configuration lives here
        //   CanvasModelViewer.exe log --level debug --file mylog.txt --console --fps
        auto& logCmd = rootCmd.AddSubCommand("log");
        logCmd.SetDescription("Configure logging output");

        logCmd.AddOption(InCommand::OptionType::Variable, "level", 'l')
            .SetDescription("Set logging level")
            .SetDomain({"trace", "debug", "info", "warn", "error", "critical", "off"})
            .BindTo(logLevel);

        logCmd.AddOption(InCommand::OptionType::Variable, "file", 'f')
            .SetDescription("Log file path (default: CanvasModelViewer.log next to executable)")
            .BindTo(logFile);

        logCmd.AddOption(InCommand::OptionType::Switch, "console", 'c')
            .SetDescription("Spawn a console window with live log output")
            .BindTo(logConsole);

        logCmd.AddOption(InCommand::OptionType::Switch, "fps")
            .SetDescription("Log FPS to the log output")
            .BindTo(logFps);

        // Capture help text to a stream — no console is attached to write to directly
        std::ostringstream helpStream;
        cmdParser.EnableAutoHelp("help", 'h', helpStream);
        
        // Parse the command line
        cmdParser.ParseArgs(argc, argv.data());
        
        // If help was requested, show in a dedicated console window
        if (cmdParser.WasAutoHelpRequested())
        {
            AllocConsole();
            SetConsoleTitleA("CanvasModelViewer - Help");
            FILE* tmpOut = nullptr;
            FILE* tmpIn = nullptr;
            freopen_s(&tmpOut, "CONOUT$", "w", stdout);
            freopen_s(&tmpIn, "CONIN$", "r", stdin);
            std::cout << helpStream.str() << std::endl;
            std::cout << "Press any key to close..." << std::endl;
            (void)_getch();
            if (tmpOut) fclose(tmpOut);
            if (tmpIn) fclose(tmpIn);
            FreeConsole();
            return 0;
        }
    }
    catch (const InCommand::SyntaxException& e)
    {
        std::ostringstream oss;
        oss << "Command line error: " << e.GetMessage();
        if (!e.GetToken().empty())
            oss << " (token: '" << e.GetToken() << "')";
        MessageBoxA(nullptr, oss.str().c_str(), "CanvasModelViewer", MB_OK | MB_ICONERROR);
        return -1;
    }
    catch (const InCommand::ApiException& e)
    {
        std::ostringstream oss;
        oss << "Internal error: " << e.GetMessage();
        MessageBoxA(nullptr, oss.str().c_str(), "CanvasModelViewer", MB_OK | MB_ICONERROR);
        return -1;
    }

    // Map log level string to QLog::Level
    QLog::Level qlogLevel = QLog::Level::Warn; // default
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

    // Determine log file path — default to a timestamped file next to the executable
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
        std::string filename = std::string("CanvasModelViewer_") + timeBuf + ".log";
        logFilePath = std::filesystem::path(exePathBuf).parent_path() / filename;
    }
    else
        logFilePath = logFile;

    // Spawn optional console window for live log output (--log-console)
    std::unique_ptr<CConsole> pConsole;
    if (logConsole)
        pConsole = std::make_unique<CConsole>();

    // Initialize logger — writes to OutputDebugString always, log file, and optional console
    CLogSink logSink(logFilePath, logConsole);
    auto qlogLogger = std::make_unique<QLog::Logger>(logSink, qlogLevel);
    
    // Create QLogAdapter to wrap QLog logger for Canvas
    Gem::TGemPtr<Canvas::QLogAdapter> pAdapter;
    Gem::ThrowGemError(Gem::TGenericImpl<Canvas::QLogAdapter>::Create(&pAdapter, qlogLogger.release()));
    
    Gem::TGemPtr<Canvas::XLogger> pLogger;
    pAdapter->QueryInterface(&pLogger);

    Canvas::LogInfo(pLogger.Get(), "Log file: %s", logFilePath.string().c_str());
    Canvas::LogInfo(pLogger.Get(), "Startup options: --fbx='%s', --exitframecount=%d, log level='%s'",
        fbxPath.c_str(), exitFrameCount, logLevel.c_str());

    std::error_code cwdEc;
    const std::filesystem::path cwdPath = std::filesystem::current_path(cwdEc);
    Canvas::LogInfo(pLogger.Get(), "Startup working directory: %s",
        cwdEc ? "<unavailable>" : cwdPath.string().c_str());

    // Register the application window class
    char szTitle[MAX_LOADSTRING];
    LoadStringA(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    ThinWin::CWindow::RegisterWindowClass(hInstance);

    // Create the application object
    std::filesystem::path modelPath;
    if (!fbxPath.empty())
        modelPath = std::filesystem::u8path(fbxPath);

    std::unique_ptr<CApp> pApp(std::make_unique<CApp>(hInstance, szTitle, pLogger, exitFrameCount, logFps, modelPath));

    // Initialize the application
    if (!pApp->Initialize(nCmdShow))
    {
        Canvas::LogError(pLogger.Get(), "Application initialization failed; exiting");
        comCleanup();
        return FALSE;
    }

    int rc = pApp->Execute();
    pApp.reset();
    comCleanup();
    return rc;
}
