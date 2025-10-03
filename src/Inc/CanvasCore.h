//================================================================================================
// CanvasCore
//================================================================================================

#pragma once
#include <QLog.h>
#include "CanvasGfx.h"
#include "CanvasModel.h"

namespace Canvas
{
#define FOR_EACH_CANVAS_INTERFACE(macro, ...) \
    macro(XCanvas, __VA_ARGS__) \
    macro(XCanvasElement, __VA_ARGS__) \
    macro(XScene, __VA_ARGS__) \
    macro(XSceneGraphNode, __VA_ARGS__) \
    macro(XMeshInstance, __VA_ARGS__) \
    macro(XLight, __VA_ARGS__) \
    macro(XTransform, __VA_ARGS__) \
    macro(XCamera, __VA_ARGS__) \
    macro(XMaterial, __VA_ARGS__) \
    macro(XMesh, __VA_ARGS__) \
    macro(XRenderable, __VA_ARGS__)

#define FORWARD_DECLARE_INTERFACE_STRUCT(xface, _) \
    struct xface;

FOR_EACH_CANVAS_INTERFACE(FORWARD_DECLARE_INTERFACE_STRUCT)

//------------------------------------------------------------------------------------------------
struct
XCanvas : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(0x0F215E5907B4651D);

    GEMMETHOD(InitCanvasGfx)(PCSTR szDLLPath, _Outptr_opt_result_nullonfailure_ XGfxInstance **ppCanvasGfx) = 0;
    GEMMETHOD(FrameTick)() = 0;

    GEMMETHOD(CreateScene)(XScene **ppScene) = 0;
    GEMMETHOD(CreateSceneGraphNode)(XSceneGraphNode **ppNode) = 0;
    GEMMETHOD(CreateTransform)(XTransform **ppTransform) = 0;
    GEMMETHOD(CreateCamera)(const ModelData::CAMERA_DATA &cameraData, XCamera **ppCamera) = 0;
    GEMMETHOD(CreateLight)(const ModelData::LIGHT_DATA &lightData, XLight **ppLight) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XCanvasElement : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(0x5604F8425EBF3A75);

    GEMMETHOD_(PCSTR, GetName)() = 0;
    GEMMETHOD_(void, SetName)(PCSTR szName) = 0;

    GEMMETHOD_(XCanvas *, GetCanvas)() = 0;
};

//------------------------------------------------------------------------------------------------
struct
XMaterial : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(0xD6E17B2CB8454154);

    GEMMETHOD(Initialize)() = 0;
};

//------------------------------------------------------------------------------------------------
struct
XMesh : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(0x7EBC2A5A40CC96D3);
};

//------------------------------------------------------------------------------------------------
enum class RotationType
{
    EulerXYZ,
    EulerXZY,
    EulerYXZ,
    EulerYZX,
    EulerZXY,
    EulerZYX,
    QuaternionWXYZ,
    AxisAngle,
};

//------------------------------------------------------------------------------------------------
struct
XTransform : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(0x2A08BD07EC525C0B);

    GEMMETHOD_(RotationType, GetRotationType)() const = 0;
    GEMMETHOD_(const Math::FloatVector4 &, GetRotation)() const = 0;
    GEMMETHOD_(const Math::FloatVector4 &, GetTranslation)() const = 0;
    GEMMETHOD_(void, SetRotation)(RotationType Type, _In_ const Math::FloatVector4 &Rotation) = 0;
    GEMMETHOD_(void, SetTranslation)(_In_ const Math::FloatVector4 &Translation) = 0;
    GEMMETHOD(LookAt)(_In_ const Math::FloatVector4 &Location) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XRenderable : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(0xF0449B8912467DD4);
};

//------------------------------------------------------------------------------------------------
struct
XMeshInstance : public XRenderable
{
    GEM_INTERFACE_DECLARE(0xB727EFEA527A1032);

    GEMMETHOD_(void, SetMesh)(XMesh *pMesh) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XCamera : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(0x4F4481985210AE1E);
};

//------------------------------------------------------------------------------------------------
struct
XLight : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(0x97EC7872FDAD30F2);
};

//------------------------------------------------------------------------------------------------
struct
XSceneGraphNode : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(0x86E8F764FE09E772);

    GEMMETHOD(AddChild)(_In_ XSceneGraphNode *pChild) = 0;

    GEMMETHOD_(XSceneGraphNode *, GetParent)() = 0;
    GEMMETHOD_(XSceneGraphNode *, GetSibling)() = 0;
    GEMMETHOD_(XSceneGraphNode *, GetFirstChild)() = 0;

    GEMMETHOD_(void, SetTransform)(XTransform *pTransform) = 0;
    GEMMETHOD_(XTransform *, GetTransform)() const = 0;

    GEMMETHOD_(void, SetRenderable)(XRenderable *pRenderable) = 0;
    GEMMETHOD_(XRenderable *, GetMesh)() = 0;

    GEMMETHOD_(void, SetCamera)(XCamera *pMesh) = 0;
    GEMMETHOD_(XCamera *, GetCamera)() = 0;

    GEMMETHOD_(void, SetLight)(XLight *pMesh) = 0;
    GEMMETHOD_(XLight *, GetLight)() = 0;
};

//------------------------------------------------------------------------------------------------
struct
XScene : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(0x0A470E86351AF96A);

    GEMMETHOD_(XSceneGraphNode *, GetRootSceneGraphNode)() = 0;
};

//------------------------------------------------------------------------------------------------
// Helper classes
class CFunctionSentinel
{
    std::shared_ptr<QLog::Logger> m_Logger;
    QLog::Level m_DefaultLogLevel;
    const char *m_FunctionName;
    Gem::Result m_Result = Gem::Result::Success;

public:
    CFunctionSentinel(std::shared_ptr<QLog::Logger> logger, const char *FunctionName, QLog::Level DefaultLogLevel = QLog::Level::Info) :
        m_FunctionName(FunctionName),
        m_Logger(logger),
        m_DefaultLogLevel(DefaultLogLevel)
    {
        if (m_Logger) {
            m_Logger->Log(m_DefaultLogLevel, "Begin: %s", m_FunctionName);
        }
    }
    ~CFunctionSentinel()
    {
        if (m_Logger) {
            QLog::Level level = Gem::Failed(m_Result) ? QLog::Level::Error : m_DefaultLogLevel;
            m_Logger->Log(level, "%s: %s", GemResultString(m_Result), m_FunctionName);
        }
    }

    void SetResultCode(Gem::Result Result) { m_Result = Result; }
};

extern Gem::Result GEMAPI CreateCanvas(XCanvas **ppCanvas, std::shared_ptr<QLog::Logger> pLogger = nullptr);

}

