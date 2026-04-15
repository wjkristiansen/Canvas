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
class CApp
{
    std::string m_Title;
    HINSTANCE m_hInstance;
    Gem::TGemPtr<Canvas::XLogger> m_pLogger;
    std::unique_ptr<ThinWin::CWindow> m_pWindow;
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
    Gem::TGemPtr<Canvas::XGfxUIGraph> m_pUIGraph;
    Gem::TGemPtr<Canvas::XGfxUIRectElement> m_pHudPanel;
    Gem::TGemPtr<Canvas::XGfxUITextElement> m_pTitleText;
    Gem::TGemPtr<Canvas::XGfxUITextElement> m_pFpsText;
    int m_exitFrameCount;  // -1 means don't exit automatically; >= 0 means exit after N frames
    bool m_logFps;
    float m_fps = 0.0f;
    std::string m_fpsString;
    std::filesystem::path m_ModelPath;

    std::vector<Gem::TGemPtr<Canvas::XGfxMeshData>> m_ImportedMeshData;
    std::vector<Gem::TGemPtr<Canvas::XMeshInstance>> m_ImportedMeshInstances;
    std::vector<Gem::TGemPtr<Canvas::XSceneGraphNode>> m_ImportedNodes;
    std::vector<Gem::TGemPtr<Canvas::XLight>> m_ImportedLights;
    std::vector<Gem::TGemPtr<Canvas::XCamera>> m_ImportedCameras;

    // Camera controller state
    float m_CameraYaw = 0.0f;    // Radians, around world Z (up)
    float m_CameraPitch = 0.0f;  // Radians, around camera right
    bool m_MouseCaptured = false;
    POINT m_LastCursorPos = {};

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
        Canvas::XGfxDevice *pDevice,
        Canvas::XGfxRenderQueue *pGfxRenderQueue,
        Canvas::XCamera *pDefaultCamera,
        Canvas::XSceneGraphNode *pDefaultCameraNode)
    {
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

        std::vector<Gem::TGemPtr<Canvas::XGfxMeshData>> meshDataByIndex(imported.Meshes.size());
        for (size_t meshIndex = 0; meshIndex < imported.Meshes.size(); ++meshIndex)
        {
            const Canvas::Fbx::ImportedMesh &srcMesh = imported.Meshes[meshIndex];
            if (srcMesh.Positions.empty() || srcMesh.Positions.size() != srcMesh.Normals.size())
            {
                Canvas::LogWarn(m_pLogger.Get(), "Skipping imported mesh '%s': invalid vertex streams", srcMesh.Name.c_str());
                continue;
            }

            Gem::TGemPtr<Canvas::XGfxMeshData> pMeshData;
            Gem::ThrowGemError(pDevice->CreateMeshData(
                static_cast<uint32_t>(srcMesh.Positions.size()),
                srcMesh.Positions.data(),
                srcMesh.Normals.data(),
                pGfxRenderQueue,
                &pMeshData,
                srcMesh.Name.c_str()));

            meshDataByIndex[meshIndex].Attach(pMeshData.Detach());
            m_ImportedMeshData.push_back(meshDataByIndex[meshIndex]);
        }

        std::vector<Gem::TGemPtr<Canvas::XSceneGraphNode>> nodes(imported.Nodes.size());
        std::vector<Gem::TGemPtr<Canvas::XLight>> lightsByIndex(imported.Lights.size());
        std::vector<Gem::TGemPtr<Canvas::XCamera>> camerasByIndex(imported.Cameras.size());

        for (size_t lightIndex = 0; lightIndex < imported.Lights.size(); ++lightIndex)
        {
            const Canvas::Fbx::ImportedLight &srcLight = imported.Lights[lightIndex];
            const std::string lightName = srcLight.Name.empty() ? ("ImportedLight_" + std::to_string(lightIndex)) : srcLight.Name;

            const char* lightType = "Unknown";
            switch (srcLight.Type)
            {
            case Canvas::LightType::Point: lightType = "Point"; break;
            case Canvas::LightType::Directional: lightType = "Directional"; break;
            case Canvas::LightType::Spot: lightType = "Spot"; break;
            case Canvas::LightType::Ambient: lightType = "Ambient"; break;
            case Canvas::LightType::Area: lightType = "Area"; break;
            default: break;
            }

            Canvas::LogInfo(m_pLogger.Get(),
                "Imported light values: name='%s' type=%s color=(%.6f, %.6f, %.6f) intensity=%.6f range=%.6f attenuation=(%.6f, %.6f, %.6f) spot=(inner=%.6f outer=%.6f)",
                lightName.c_str(),
                lightType,
                srcLight.Color[0], srcLight.Color[1], srcLight.Color[2],
                srcLight.Intensity,
                srcLight.Range,
                srcLight.AttenuationConst,
                srcLight.AttenuationLinear,
                srcLight.AttenuationQuad,
                srcLight.SpotInnerAngle,
                srcLight.SpotOuterAngle);

            Gem::TGemPtr<Canvas::XLight> pLight;
            Gem::ThrowGemError(pCanvas->CreateLight(srcLight.Type, &pLight, lightName.c_str()));
            pLight->SetColor(srcLight.Color);
            pLight->SetIntensity(srcLight.Intensity);
            pLight->SetRange(srcLight.Range);
            pLight->SetAttenuation(srcLight.AttenuationConst, srcLight.AttenuationLinear, srcLight.AttenuationQuad);
            if (srcLight.Type == Canvas::LightType::Spot)
                pLight->SetSpotAngles(srcLight.SpotInnerAngle, srcLight.SpotOuterAngle);

            lightsByIndex[lightIndex].Attach(pLight.Detach());
            m_ImportedLights.push_back(lightsByIndex[lightIndex]);
        }

        for (size_t cameraIndex = 0; cameraIndex < imported.Cameras.size(); ++cameraIndex)
        {
            const Canvas::Fbx::ImportedCamera &srcCamera = imported.Cameras[cameraIndex];
            const std::string cameraName = srcCamera.Name.empty() ? ("ImportedCamera_" + std::to_string(cameraIndex)) : srcCamera.Name;

            Gem::TGemPtr<Canvas::XCamera> pImportedCamera;
            Gem::ThrowGemError(pCanvas->CreateCamera(&pImportedCamera, cameraName.c_str()));
            pImportedCamera->SetNearClip(srcCamera.NearClip);
            pImportedCamera->SetFarClip(srcCamera.FarClip);
            pImportedCamera->SetFovAngle(srcCamera.FovAngle);
            pImportedCamera->SetAspectRatio(srcCamera.AspectRatio);

            camerasByIndex[cameraIndex].Attach(pImportedCamera.Detach());
            m_ImportedCameras.push_back(camerasByIndex[cameraIndex]);
        }

        for (size_t nodeIndex = 0; nodeIndex < imported.Nodes.size(); ++nodeIndex)
        {
            const Canvas::Fbx::ImportedNode &srcNode = imported.Nodes[nodeIndex];
            const std::string nodeName = srcNode.Name.empty() ? ("ImportedNode_" + std::to_string(nodeIndex)) : srcNode.Name;

            Gem::TGemPtr<Canvas::XSceneGraphNode> pNode;
            Gem::ThrowGemError(pCanvas->CreateSceneGraphNode(&pNode, nodeName.c_str()));
            pNode->SetName(nodeName.c_str());
            pNode->SetLocalTranslation(srcNode.Translation);
            pNode->SetLocalRotation(srcNode.Rotation);
            pNode->SetLocalScale(srcNode.Scale);

            if (srcNode.MeshIndex >= 0 && static_cast<size_t>(srcNode.MeshIndex) < meshDataByIndex.size())
            {
                Canvas::XGfxMeshData *pMeshData = meshDataByIndex[static_cast<size_t>(srcNode.MeshIndex)].Get();
                if (pMeshData)
                {
                    const std::string meshInstanceName = nodeName + "_Mesh";
                    Gem::TGemPtr<Canvas::XMeshInstance> pMeshInstance;
                    Gem::ThrowGemError(pCanvas->CreateMeshInstance(&pMeshInstance, meshInstanceName.c_str()));
                    pMeshInstance->SetMeshData(pMeshData);
                    Gem::ThrowGemError(pNode->BindElement(pMeshInstance));
                    m_ImportedMeshInstances.push_back(pMeshInstance);
                    LogNodeTransform("Imported mesh node", pNode);
                }
            }

            if (srcNode.LightIndex >= 0 && static_cast<size_t>(srcNode.LightIndex) < lightsByIndex.size())
            {
                Canvas::XLight *pLight = lightsByIndex[static_cast<size_t>(srcNode.LightIndex)].Get();
                if (pLight)
                {
                    Gem::ThrowGemError(pNode->BindElement(pLight));
                    LogNodeTransform("Imported light node", pNode);
                }
            }

            if (srcNode.CameraIndex >= 0 && static_cast<size_t>(srcNode.CameraIndex) < camerasByIndex.size())
            {
                Canvas::XCamera *pImportedCamera = camerasByIndex[static_cast<size_t>(srcNode.CameraIndex)].Get();
                if (pImportedCamera)
                {
                    Gem::ThrowGemError(pNode->BindElement(pImportedCamera));
                }
            }

            nodes[nodeIndex].Attach(pNode.Detach());
        }

        for (size_t nodeIndex = 0; nodeIndex < imported.Nodes.size(); ++nodeIndex)
        {
            const int32_t parentIndex = imported.Nodes[nodeIndex].ParentIndex;
            if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < nodes.size())
                nodes[static_cast<size_t>(parentIndex)]->AddChild(nodes[nodeIndex]);
            else
                pScene->GetRootSceneGraphNode()->AddChild(nodes[nodeIndex]);
        }

        m_ImportedNodes = std::move(nodes);

        Canvas::XCamera *pSelectedCamera = nullptr;
        Gem::TGemPtr<Canvas::XCamera> selectedCameraRef;

        // Prefer the importer-selected active camera node when available.
        if (imported.ActiveCameraNodeIndex >= 0 && static_cast<size_t>(imported.ActiveCameraNodeIndex) < imported.Nodes.size())
        {
            const Canvas::Fbx::ImportedNode &activeNode = imported.Nodes[static_cast<size_t>(imported.ActiveCameraNodeIndex)];
            if (activeNode.CameraIndex >= 0 && static_cast<size_t>(activeNode.CameraIndex) < camerasByIndex.size())
            {
                pSelectedCamera = camerasByIndex[static_cast<size_t>(activeNode.CameraIndex)].Get();
                selectedCameraRef = camerasByIndex[static_cast<size_t>(activeNode.CameraIndex)];
            }
        }

        // Fall back to first imported camera if no explicit active camera was found.
        if (!pSelectedCamera && !camerasByIndex.empty() && camerasByIndex[0].Get() != nullptr)
        {
            pSelectedCamera = camerasByIndex[0].Get();
            selectedCameraRef = camerasByIndex[0];
            Canvas::LogWarn(m_pLogger.Get(), "Imported scene has no explicit active camera; using first imported camera");
        }

        if (pSelectedCamera)
        {
            pScene->SetActiveCamera(pSelectedCamera);
            m_pCamera = selectedCameraRef;
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
            std::unique_ptr<ThinWin::CWindow> pWindow = std::make_unique<ThinWin::CWindow>(m_Title.c_str(), m_hInstance, WS_OVERLAPPEDWINDOW);
            if (!pWindow.get())
            {
                Gem::ThrowGemError(Gem::Result::OutOfMemory);
            }

            pWindow->ShowWindow(nCmdShow);
            pWindow->UpdateWindow();

            initStep = "create_canvas";
            Gem::TGemPtr<Canvas::XCanvas> pCanvas;
            Gem::ThrowGemError(Canvas::CreateCanvas(m_pLogger.Get(), &pCanvas));

            initStep = "create_scene";
            Gem::TGemPtr<Canvas::XSceneGraph> pScene;
            Gem::ThrowGemError(pCanvas->CreateSceneGraph(&pScene, "MainScene"));

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
            if (!TryLoadImportedScene(pCanvas, pScene, pDevice, pGfxRenderQueue, pCamera, pCameraNode))
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
            Gem::TGemPtr<Canvas::XGfxUIGraph> pUIGraph;
            Gem::ThrowGemError(pCanvas->CreateUIGraph(pDevice, pGfxRenderQueue, &pUIGraph));

            // Create UI graph nodes for positioning
            Gem::TGemPtr<Canvas::XGfxUIGraphNode> pHudNode;
            Gem::ThrowGemError(pUIGraph->CreateNode(nullptr, &pHudNode));
            pHudNode->SetLocalPosition(Canvas::Math::FloatVector2(6.0f, 6.0f));

            // HUD background panel
            Gem::TGemPtr<Canvas::XGfxUIRectElement> pHudPanel;
            Gem::ThrowGemError(pUIGraph->CreateRectElement(pHudNode, &pHudPanel));
            pHudPanel->SetSize(Canvas::Math::FloatVector2(340.0f, 70.0f));
            pHudPanel->SetFillColor(Canvas::Math::FloatVector4(0.125f, 0.125f, 0.125f, 0.75f));

            // Title text — child node of HUD
            Gem::TGemPtr<Canvas::XGfxUIGraphNode> pTitleNode;
            Gem::ThrowGemError(pUIGraph->CreateNode(pHudNode, &pTitleNode));
            pTitleNode->SetLocalPosition(Canvas::Math::FloatVector2(4.0f, 4.0f));

            Gem::TGemPtr<Canvas::XGfxUITextElement> pTitleText;
            Gem::ThrowGemError(pUIGraph->CreateTextElement(pTitleNode, &pTitleText));
            pTitleText->SetFont(pFont);
            {
                Canvas::TextLayoutConfig titleConfig;
                titleConfig.FontSize = 32.0f;
                titleConfig.Color = Canvas::Math::FloatVector4(1.0f, 1.0f, 1.0f, 1.0f);
                pTitleText->SetLayoutConfig(titleConfig);
            }
            pTitleText->SetText("Canvas Model Viewer");

            // FPS text — child node of HUD
            Gem::TGemPtr<Canvas::XGfxUIGraphNode> pFpsNode;
            Gem::ThrowGemError(pUIGraph->CreateNode(pHudNode, &pFpsNode));
            pFpsNode->SetLocalPosition(Canvas::Math::FloatVector2(4.0f, 40.0f));

            Gem::TGemPtr<Canvas::XGfxUITextElement> pFpsText;
            Gem::ThrowGemError(pUIGraph->CreateTextElement(pFpsNode, &pFpsText));
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

        // RAII guard to ensure cursor is always unclipped/shown on exit, even if we
        // terminate abnormally.  Leaving the cursor clipped or hidden is a system-wide
        // side-effect that survives the process.
        struct CursorGuard
        {
            bool* pCaptured;
            CursorGuard(bool* p) : pCaptured(p) {}
            ~CursorGuard()
            {
                if (*pCaptured)
                {
                    ClipCursor(nullptr);
                    while (ShowCursor(TRUE) < 0) {}
                    *pCaptured = false;
                }
            }
        } cursorGuard(&m_MouseCaptured);

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
                if (m_logFps) Canvas::LogInfo(m_pLogger.Get(), "FPS: %.1f", m_fps);
                fpsTime = currTime;
                fpsCounter = 0;
            }
            ++fpsCounter;
            ++frameCount;

            //==================================================================
            // Camera controller: mouse look + WASD movement
            //==================================================================
            
            // Track focus changes — release/acquire cursor as needed
            HWND foreground = GetForegroundWindow();
            HWND foregroundRoot = foreground ? GetAncestor(foreground, GA_ROOT) : nullptr;
            bool hasFocus = (foreground == m_pWindow->m_hWnd) ||
                            (foregroundRoot == m_pWindow->m_hWnd) ||
                            (foreground && IsChild(m_pWindow->m_hWnd, foreground));
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

            float dx = 0.0f;
            float dy = 0.0f;
            bool rotated = false;
            if (m_MouseCaptured)
            {
                // Mouse delta → yaw/pitch
                RECT rc;
                GetClientRect(m_pWindow->m_hWnd, &rc);
                POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
                ClientToScreen(m_pWindow->m_hWnd, &center);

                POINT cursorPos;
                GetCursorPos(&cursorPos);
                dx = static_cast<float>(cursorPos.x - center.x);
                dy = static_cast<float>(cursorPos.y - center.y);
                SetCursorPos(center.x, center.y);

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

    std::string logLevel = "info";
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
        return FALSE;
    }

    return pApp->Execute();
}
