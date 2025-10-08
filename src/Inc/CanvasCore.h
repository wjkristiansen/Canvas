//================================================================================================
// CanvasCore
//================================================================================================

#pragma once
#include <QLog.h>
#include "CanvasGfx.h"
#include "CanvasRender.h"

namespace Canvas
{

#if defined(_WIN32)
  #if defined(CANVASCORE_EXPORTS)
    #define CANVAS_API __declspec(dllexport)
  #else
    #define CANVAS_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define CANVAS_API __attribute__((visibility("default")))
#else
  #define CANVAS_API
#endif

struct XScene;
struct XCamera;
struct XLight;
struct XSceneGraphNode;

//------------------------------------------------------------------------------------------------
struct
XCanvas : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XCanvas, 0x0F215E5907B4651D);

    GEMMETHOD(InitGfx)(PCSTR path, HWND hWnd) = 0;
    GEMMETHOD(FrameTick)() = 0;

    GEMMETHOD(CreateScene)(XScene **ppScene) = 0;
    GEMMETHOD(CreateSceneGraphNode)(XSceneGraphNode **ppNode) = 0;
    GEMMETHOD(CreateCamera)(XCamera **ppCamera) = 0;
    GEMMETHOD(CreateLight)(XLight **ppLight) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XCanvasElement : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XCanvasElement, 0x5604F8425EBF3A75);

    GEMMETHOD_(PCSTR, GetName)() = 0;
    GEMMETHOD_(void, SetName)(PCSTR szName) = 0;
    GEMMETHOD_(XCanvas *, GetCanvas)() = 0;
    GEMMETHOD_(PCSTR, GetTypeName)() = 0;
};

//------------------------------------------------------------------------------------------------
struct
XMaterial : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XMaterial, 0xD6E17B2CB8454154);

    GEMMETHOD(Initialize)() = 0;
};

//------------------------------------------------------------------------------------------------
struct
XMesh : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XMesh, 0x7EBC2A5A40CC96D3);
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
XTransform : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XTransform, 0x2A08BD07EC525C0B);

    GEMMETHOD_(RotationType, GetRotationType)() const = 0;
    GEMMETHOD_(const Math::FloatVector4 &, GetRotation)() const = 0;
    GEMMETHOD_(const Math::FloatVector4 &, GetTranslation)() const = 0;
    GEMMETHOD_(void, SetRotation)(RotationType Type, _In_ const Math::FloatVector4 &Rotation) = 0;
    GEMMETHOD_(void, SetTranslation)(_In_ const Math::FloatVector4 &Translation) = 0;
    GEMMETHOD(LookAt)(_In_ const Math::FloatVector4 &Location, _In_ const Math::FloatVector4 &WorldUp) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XSceneGraphNode : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XSceneGraphNode, 0x86E8F764FE09E772);

    GEMMETHOD(AddChild)(_In_ XSceneGraphNode *pChild) = 0;

    GEMMETHOD_(XSceneGraphNode *, GetParent)() = 0;
    GEMMETHOD_(XSceneGraphNode *, GetSibling)() = 0;
    GEMMETHOD_(XSceneGraphNode *, GetFirstChild)() = 0;
};

//-----------------------0-------------------------------------------------------------------------
struct
XRenderQueue : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XRenderQueue, 0x3B35719161878DCC);

    // BUGBUG: TODO...
};

//-----------------------0-------------------------------------------------------------------------
struct
XSceneGraphElement : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XSceneGraphElement, 0xD9F48C0E3F0775F0);

    // Attach to the specified node.
    // Automatically detaches if attached to a different node.
    // The node holds a reference to this element
    GEMMETHOD(AttachTo)(XSceneGraphNode *pNode) = 0;

    // Detaches the element from a scene graph node.
    // If attached, the reference is released by the node.
    GEMMETHOD(Detach)() = 0;

    // Returns a weak pointer to the scene graph node the element is attached to
    GEMMETHOD_(XSceneGraphNode *, GetAttachedNode)() = 0;

    // Dispatches the element for rendering
    GEMMETHOD(DispatchForRender)(XRenderQueue *pRenderQueue) = 0;
};

//------------------------------------------------------------------------------------------------
// Exports
extern Gem::Result CANVAS_API CreateCanvas(XCanvas **ppCanvas);
extern CANVAS_API Gem::Result InitCanvasLogger(QLog::Sink &, QLog::Level);
extern CANVAS_API QLog::Logger *GetCanvasLogger();

//------------------------------------------------------------------------------------------------
struct
XMeshInstance : public XSceneGraphElement
{
    GEM_INTERFACE_DECLARE(XMeshInstance, 0xB727EFEA527A1032);

    GEMMETHOD_(void, SetMesh)(XMesh *pMesh) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XCamera : public XSceneGraphElement
{
    GEM_INTERFACE_DECLARE(XCamera, 0x4F4481985210AE1E);

    GEMMETHOD_(void, SetNearClip)(float nearClip) = 0;
    GEMMETHOD_(void, SetFarClip)(float nearClip) = 0;
    GEMMETHOD_(void, SetFovAngle)(float fovAngle) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XLight : public XSceneGraphElement
{
    GEM_INTERFACE_DECLARE(XLight, 0x97EC7872FDAD30F2);

    // BUGBUG: Light methods here like SetColor, SetIntensity, SetType, etc...
};

//------------------------------------------------------------------------------------------------
struct
XScene : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XScene, 0x0A470E86351AF96A);

    GEMMETHOD_(XSceneGraphNode *, GetRootSceneGraphNode)() = 0;
};

//------------------------------------------------------------------------------------------------
// Helper classes
class CFunctionSentinel
{
    QLog::Level m_DefaultLogLevel;
    const char *m_FunctionName;
    Gem::Result m_Result = Gem::Result::Success;

public:
    CFunctionSentinel(const char *FunctionName, QLog::Level DefaultLogLevel = QLog::Level::Info) :
        m_FunctionName(FunctionName),
        m_DefaultLogLevel(DefaultLogLevel)
    {
        if (GetCanvasLogger()) {
            GetCanvasLogger()->Log(m_DefaultLogLevel, "Begin: %s", m_FunctionName);
        }
    }
    ~CFunctionSentinel()
    {
        if (GetCanvasLogger()) {
        QLog::Level level = Gem::Failed(m_Result) ? QLog::Level::Error : m_DefaultLogLevel;
            GetCanvasLogger()->Log(level, "%s: %s", GemResultString(m_Result), m_FunctionName);
        }
    }

    void SetResultCode(Gem::Result Result) { m_Result = Result; }
};

}

