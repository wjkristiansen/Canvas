//================================================================================================
// CanvasCore
//
// IMPORTANT: Canvas uses ROW-VECTOR matrix convention throughout!
//
// - Vectors are row vectors: v' = v * M (not M * v)
// - Translation is stored in the BOTTOM ROW: matrix[3][0], matrix[3][1], matrix[3][2]
// - Matrix concatenation: child_to_world = parent_to_world * child_to_parent
// - For view matrices: world * view = identity (not view * world)
//
// This convention is used consistently across all Canvas math operations, scene graph
// transforms, and camera matrices. Do NOT assume column-vector convention when working
// with Canvas code.
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
// Light types (immutable - set at creation time)
enum class LightType : UINT
{
    Ambient = 0,
    Point = 1,
    Directional = 2,
    Spot = 3,
    Area = 4,
};

//------------------------------------------------------------------------------------------------
// Light flags (mutable - rendering hints)
enum LightFlags : UINT
{
    LightFlags_None = 0,
    CastsShadows = 1 << 0,
    Enabled = 1 << 1,
};

//------------------------------------------------------------------------------------------------
struct
XCanvas : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XCanvas, 0x0F215E5907B4651D);

    GEMMETHOD(InitGfx)(PCSTR path) = 0;
    GEMMETHOD(CreateGfxDevice)(XGfxDevice **ppGfxDevice) = 0;
    GEMMETHOD(CreateScene)(XScene **ppScene) = 0;
    GEMMETHOD(CreateSceneGraphNode)(XSceneGraphNode **ppNode) = 0;
    GEMMETHOD(CreateCamera)(XCamera **ppCamera) = 0;
    GEMMETHOD(CreateLight)(LightType type, XLight **ppLight) = 0;
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
enum class AnimationAttribute
{
    // Transform attributes
    PositionX,
    PositionY,
    PositionZ,
    EulerRotationX,
    EulerRotationY,
    EulerRotationZ,
    QuaternionRotation,
    ScaleX,
    ScaleY,
    ScaleZ,

    // Material attributes
    DiffuseColor,
    AmbientColor,
    EmissiveColor,
    UOffset,
    VOffset,

    // Light attributes
    Color,
    Intensity,
    AttenuationConstant,
    AttenuationLinear,
    AttenuationQuadratic,
    Range,
    SpotInnerAngle,
    SpotOuterAngle,

    // Camera attributes
    NearClip,
    FarClip,
    FovAngle,
    AspectRatio,
};

//------------------------------------------------------------------------------------------------
struct
XAnimation : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XAnimation, 0x2A83549FFDAF2D9D);

    GEMMETHOD(Evaluate(float animationTime, AnimationAttribute attribute, float *pValues)) = 0;
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

    GEMMETHOD_(const Math::FloatQuaternion &, GetLocalRotation)() const = 0;
    GEMMETHOD_(const Math::FloatVector4 &, GetLocalTranslation)() const = 0;
    GEMMETHOD_(const Math::FloatVector4 &, GetLocalScale)() const = 0;
    GEMMETHOD_(void, SetLocalRotation)(_In_ const Math::FloatQuaternion &Rotation) = 0;
    GEMMETHOD_(void, SetLocalTranslation)(_In_ const Math::FloatVector4 &Translation) = 0;
    GEMMETHOD_(void, SetLocalScale)(_In_ const Math::FloatVector4 &Scale) = 0;

    GEMMETHOD_(const Math::FloatQuaternion, GetGlobalRotation)() = 0;
    GEMMETHOD_(const Math::FloatVector4, GetGlobalTranslation)() = 0;
    GEMMETHOD_(const Math::FloatMatrix4x4, GetGlobalMatrix)() = 0;
    GEMMETHOD_(const Math::FloatMatrix4x4, GetLocalMatrix)() = 0;

    GEMMETHOD(Update)(float dtime) = 0;
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

    GEMMETHOD(Update)(float dtime) = 0;
};

//------------------------------------------------------------------------------------------------
// Exports
extern Gem::Result CANVAS_API CreateCanvas(XCanvas **ppCanvas);
extern CANVAS_API Gem::Result InitCanvasLogger(QLog::Sink &, QLog::Level);
extern CANVAS_API QLog::Logger *GetCanvasLogger();

//------------------------------------------------------------------------------------------------
struct
XCamera : public XSceneGraphElement
{
    GEM_INTERFACE_DECLARE(XCamera, 0x4F4481985210AE1E);

    GEMMETHOD_(void, SetNearClip)(float nearClip) = 0;
    GEMMETHOD_(void, SetFarClip)(float nearClip) = 0;
    GEMMETHOD_(void, SetFovAngle)(float fovAngle) = 0;
    GEMMETHOD_(void, SetAspectRatio)(float aspectRatio) = 0;

    GEMMETHOD_(float, GetNearClip)() = 0;
    GEMMETHOD_(float, GetFarClip)() = 0;
    GEMMETHOD_(float, GetFovAngle)() = 0;
    GEMMETHOD_(float, GetAspectRatio)() = 0;

    GEMMETHOD_(Math::FloatMatrix4x4, GetViewMatrix)() = 0;
    GEMMETHOD_(Math::FloatMatrix4x4, GetProjectionMatrix)() = 0;
    GEMMETHOD_(Math::FloatMatrix4x4, GetViewProjectionMatrix)() = 0;
};

//------------------------------------------------------------------------------------------------
struct
XLight : public XSceneGraphElement
{
    GEM_INTERFACE_DECLARE(XLight, 0x97EC7872FDAD30F2);

    // Immutable attributes (set at creation)
    GEMMETHOD_(LightType, GetType)() const = 0;

    // Mutable attributes
    GEMMETHOD_(void, SetColor)(const Math::FloatVector4& color) = 0;
    GEMMETHOD_(Math::FloatVector4, GetColor)() const = 0;

    GEMMETHOD_(void, SetIntensity)(float intensity) = 0;
    GEMMETHOD_(float, GetIntensity)() const = 0;

    GEMMETHOD_(void, SetFlags)(UINT flags) = 0;
    GEMMETHOD_(UINT, GetFlags)() const = 0;

    // Attenuation (for Point and Spot lights)
    GEMMETHOD_(void, SetRange)(float range) = 0;
    GEMMETHOD_(float, GetRange)() const = 0;

    GEMMETHOD_(void, SetAttenuation)(float constant, float linear, float quadratic) = 0;
    GEMMETHOD_(void, GetAttenuation)(float* pConstant, float* pLinear, float* pQuadratic) const = 0;

    // Spot light parameters (only valid for Spot lights)
    GEMMETHOD_(void, SetSpotAngles)(float innerAngle, float outerAngle) = 0;
    GEMMETHOD_(void, GetSpotAngles)(float* pInnerAngle, float* pOuterAngle) const = 0;
};

//------------------------------------------------------------------------------------------------
struct
XScene : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XScene, 0x0A470E86351AF96A);

    GEMMETHOD_(XSceneGraphNode *, GetRootSceneGraphNode)() = 0;

    GEMMETHOD(Update)(float dtime) = 0;
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

