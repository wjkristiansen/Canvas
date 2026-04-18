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
#include "CanvasMath.hpp"
#include <cstdarg>

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

// Forward declarations
struct XSceneGraph;
struct XCamera;
struct XLight;
struct XGfxBuffer;
struct XGfxMeshData;
struct XMeshInstance;
struct XSceneGraphNode;
struct XSceneGraphElement;
struct XGfxRenderQueue;
struct XGfxDevice;
struct XFont;
struct XUIGraph;
struct XUIGraphNode;
struct XUIElement;
struct XUITextElement;
struct XUIRectElement;
struct GfxResourceAllocation;
struct XGfxSurface;
class CGlyphCache;

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
// Log severity levels (ABI-safe, matches QLog::Level)
enum class LogLevel : UINT8
{
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

//------------------------------------------------------------------------------------------------
// XLogger - ABI-safe logging interface
struct XLogger : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XLogger, 0x7A3B8C4D9E2F1A5B);

    GEMMETHOD_(void, Log)(LogLevel level, PCSTR format, va_list args) = 0;
    GEMMETHOD_(void, SetLevel)(LogLevel level) = 0;
    GEMMETHOD_(LogLevel, GetLevel)() = 0;
    GEMMETHOD_(void, Flush)() = 0;
};

//------------------------------------------------------------------------------------------------
// XLogger helper functions

inline void LogTrace(XLogger* logger, PCSTR format, ...)
{
    if (logger)
    {
        va_list args;
        va_start(args, format);
        logger->Log(LogLevel::Trace, format, args);
        va_end(args);
    }
}

inline void LogDebug(XLogger* logger, PCSTR format, ...)
{
    if (logger)
    {
        va_list args;
        va_start(args, format);
        logger->Log(LogLevel::Debug, format, args);
        va_end(args);
    }
}

inline void LogInfo(XLogger* logger, PCSTR format, ...)
{
    if (logger)
    {
        va_list args;
        va_start(args, format);
        logger->Log(LogLevel::Info, format, args);
        va_end(args);
    }
}

inline void LogWarn(XLogger* logger, PCSTR format, ...)
{
    if (logger)
    {
        va_list args;
        va_start(args, format);
        logger->Log(LogLevel::Warn, format, args);
        va_end(args);
    }
}

inline void LogError(XLogger* logger, PCSTR format, ...)
{
    if (logger)
    {
        va_list args;
        va_start(args, format);
        logger->Log(LogLevel::Error, format, args);
        va_end(args);
    }
}

inline void LogCritical(XLogger* logger, PCSTR format, ...)
{
    if (logger)
    {
        va_list args;
        va_start(args, format);
        logger->Log(LogLevel::Critical, format, args);
        va_end(args);
    }
}

//------------------------------------------------------------------------------------------------
struct
XCanvas : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XCanvas, 0x0F215E5907B4651D);

    GEMMETHOD(LoadPlugin)(PCSTR path, struct XCanvasPlugin **ppPlugin) = 0;

    GEMMETHOD(CreateSceneGraph)(XGfxDevice *pDevice, XSceneGraph **ppScene, PCSTR name = nullptr) = 0;
    GEMMETHOD(CreateSceneGraphNode)(XSceneGraphNode **ppNode, PCSTR name = nullptr) = 0;
    GEMMETHOD(CreateCamera)(XCamera **ppCamera, PCSTR name = nullptr) = 0;
    GEMMETHOD(CreateLight)(LightType type, XLight **ppLight, PCSTR name = nullptr) = 0;
    GEMMETHOD(CreateMeshInstance)(XMeshInstance **ppMeshInstance, PCSTR name = nullptr) = 0;

    // Text/UI factory methods
    GEMMETHOD(CreateFont)(const uint8_t* pTTFData, size_t dataSize, PCSTR name, XFont** ppFont) = 0;
    GEMMETHOD(CreateUIGraph)(XGfxDevice* pDevice, XUIGraph** ppGraph) = 0;
    GEMMETHOD(CreateTextElement)(XGfxSurface* pAtlasSurface, XUITextElement** ppElement) = 0;
    GEMMETHOD(CreateRectElement)(XUIRectElement** ppElement) = 0;
    
    // Element registration methods - ONLY call from XCanvasElement::Register/Unregister implementations
    // External code should call element->Register(canvas), NOT canvas->RegisterElement(element)
    GEMMETHOD(RegisterElement)(struct XCanvasElement *) = 0;
    GEMMETHOD(UnregisterElement)(struct XCanvasElement *) = 0;

    GEMMETHOD_(XLogger *, GetLogger)() = 0;
    GEMMETHOD_(CGlyphCache*, GetGlyphCache)() = 0;
};

//------------------------------------------------------------------------------------------------
struct
XNamedElement : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XNamedElement, 0x314EEA856A888F9D);

    GEMMETHOD_(PCSTR, GetName)() = 0;
    GEMMETHOD_(void, SetName)(PCSTR szName) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XCanvasElement : public XNamedElement
{
    GEM_INTERFACE_DECLARE(XCanvasElement, 0x5604F8425EBF3A75);

    GEMMETHOD_(XCanvas *, GetCanvas)() = 0;
    GEMMETHOD_(PCSTR, GetTypeName)() = 0;

    // Public API for element registration - external code should call these methods
    GEMMETHOD(Register)(XCanvas *pCanvas) = 0;
    GEMMETHOD(Unregister)() = 0;
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
    GEMMETHOD(RemoveChild)(_In_ XSceneGraphNode *pChild) = 0;
    GEMMETHOD(InsertChildBefore)(_In_ XSceneGraphNode *pChild, _In_ XSceneGraphNode *pSibling) = 0;
    GEMMETHOD(InsertChildAfter)(_In_ XSceneGraphNode *pChild, _In_ XSceneGraphNode *pSibling) = 0;
    GEMMETHOD(BindElement)(_In_ XSceneGraphElement *pElement) = 0;

    GEMMETHOD_(XSceneGraphNode *, GetParent)() = 0;
    GEMMETHOD_(XSceneGraphNode *, GetFirstChild)() = 0;
    GEMMETHOD_(XSceneGraphNode *, GetNextChild)(_In_ XSceneGraphNode *pCurrent) = 0;
    GEMMETHOD_(XSceneGraphNode *, GetPrevChild)(_In_ XSceneGraphNode *pCurrent) = 0;

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

    // Element iteration (bound elements on this node)
    GEMMETHOD_(UINT, GetBoundElementCount)() = 0;
    GEMMETHOD_(XSceneGraphElement *, GetBoundElement)(UINT index) = 0;

    GEMMETHOD(Update)(float dtime) = 0;
};

// Text vertex structure — shared between CanvasCore (vertex generation) and CanvasGfx (rendering)
// Layout must match HLSL StructuredBuffer<TextVertex> in VSText.hlsl
struct TextVertex
{
    Math::FloatVector3 Position;    // Screen-space pixel position (12 bytes)
    Math::FloatVector2 TexCoord;    // Atlas UV coordinates (8 bytes)
    float Color[4];                 // RGBA float color (16 bytes)

    TextVertex() : Color{1.0f, 1.0f, 1.0f, 1.0f} {}

    void SetColor(const Math::FloatVector4& c) { Color[0] = c.X; Color[1] = c.Y; Color[2] = c.Z; Color[3] = c.W; }
    void SetColor(float r, float g, float b, float a) { Color[0] = r; Color[1] = g; Color[2] = b; Color[3] = a; }
};

// Draw command for batched UI text rendering from a persistent vertex buffer
struct UITextDrawCommand
{
    uint32_t StartVertex;   // Offset into persistent vertex buffer
    uint32_t VertexCount;   // Number of vertices to draw
};

// Draw command for batched UI rect rendering from a persistent vertex buffer
struct UIRectDrawCommand
{
    uint32_t StartVertex;   // Offset into persistent rect vertex buffer
    uint32_t VertexCount;   // Number of vertices to draw
};

//------------------------------------------------------------------------------------------------
struct
XSceneGraphElement : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XSceneGraphElement, 0xD9F48C0E3F0775F0);

    // Detaches the element from a scene graph node.
    // If attached, the reference is released by the node.
    GEMMETHOD(Detach)() = 0;

    // Returns a weak pointer to the scene graph node the element is attached to
    GEMMETHOD_(XSceneGraphNode *, GetAttachedNode)() = 0;

    // Called by the node when the element is attached or node transforms become dirty
    // This is called both when binding and when transforms invalidate
    // Elements can query node transforms via GetAttachedNode() when needed
    GEMMETHOD(NotifyNodeContextChanged)(_In_ XSceneGraphNode *pNode) = 0;

    GEMMETHOD(Update)(float dtime) = 0;
};

//------------------------------------------------------------------------------------------------
// Exports
extern Gem::Result CANVAS_API CreateCanvas(XLogger* pLogger, XCanvas **ppCanvas);

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
XSceneGraph : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XSceneGraph, 0x0A470E86351AF96A);

    GEMMETHOD_(XGfxDevice *, GetDevice)() = 0;
    GEMMETHOD_(XSceneGraphNode *, GetRootSceneGraphNode)() = 0;

    GEMMETHOD_(void, SetActiveCamera)(XCamera *pCamera) = 0;
    GEMMETHOD_(XCamera *, GetActiveCamera)() const = 0;

    GEMMETHOD(Update)(float dtime) = 0;
    GEMMETHOD(SubmitRenderables)(XGfxRenderQueue *pRenderQueue) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XMeshInstance : public XSceneGraphElement
{
    GEM_INTERFACE_DECLARE(XMeshInstance, 0x1C80317FA3B1799D);
    
    GEMMETHOD_(XGfxMeshData *, GetMeshData)() = 0;
    GEMMETHOD_(void, SetMeshData)(XGfxMeshData *pMesh) = 0;
    GEMMETHOD_(uint32_t, GetMaterialGroupIndex)() = 0;
};

//================================================================================================
// UI Graph - Hierarchical UI/HUD elements with dirty tracking
//================================================================================================

//------------------------------------------------------------------------------------------------
// XFont - Font resource interface
//------------------------------------------------------------------------------------------------

struct XFont : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XFont, 0x7C3F5B8A2E1D9F06);

    GEMMETHOD_(float, GetAscender)() = 0;
    GEMMETHOD_(float, GetDescender)() = 0;
    GEMMETHOD_(float, GetLineGap)() = 0;
    GEMMETHOD_(uint16_t, GetUnitsPerEm)() = 0;
};

//------------------------------------------------------------------------------------------------
// Text layout configuration
//------------------------------------------------------------------------------------------------

struct TextLayoutConfig
{
    Math::FloatVector4 Color;    // RGBA float (0..1 per channel)
    float FontSize;              // Size in pixels
    float LineHeight;            // Multiplier of font's line gap
    bool DisableKerning;

    TextLayoutConfig()
        : Color(1.0f, 1.0f, 1.0f, 1.0f), FontSize(16.0f), LineHeight(1.0f), DisableKerning(false)
    {}
};

//------------------------------------------------------------------------------------------------
enum class UIElementType : uint32_t
{
    Root = 0,
    Text,
    Rect,
};

//================================================================================================
// UI Graph - Hierarchical UI/HUD elements with dirty tracking
//================================================================================================

//------------------------------------------------------------------------------------------------
struct XUIElement : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XUIElement, 0xA1B2C3D4E5F60718);

    GEMMETHOD_(UIElementType, GetType)() const = 0;
    GEMMETHOD_(bool, IsVisible)() const = 0;
    GEMMETHOD_(void, SetVisible)(bool visible) = 0;
    GEMMETHOD_(XUIGraphNode*, GetAttachedNode)() = 0;

    // GPU vertex buffer (ready to draw, managed by graph + device)
    GEMMETHOD_(const GfxResourceAllocation&, GetVertexBuffer)() const = 0;
    GEMMETHOD_(void, SetVertexBuffer)(const GfxResourceAllocation& buffer) = 0;
};

//------------------------------------------------------------------------------------------------
struct XUITextElement : public XUIElement
{
    GEM_INTERFACE_DECLARE(XUITextElement, 0xB2C3D4E5F6071829);

    GEMMETHOD_(void, SetText)(PCSTR utf8Text) = 0;
    GEMMETHOD_(PCSTR, GetText)() const = 0;
    GEMMETHOD_(void, SetFont)(XFont* pFont) = 0;
    GEMMETHOD_(void, SetLayoutConfig)(const TextLayoutConfig& config) = 0;
    GEMMETHOD_(const TextLayoutConfig&, GetLayoutConfig)() const = 0;

    GEMMETHOD_(XGfxSurface*, GetAtlasSurface)() = 0;
};

//------------------------------------------------------------------------------------------------
struct XUIRectElement : public XUIElement
{
    GEM_INTERFACE_DECLARE(XUIRectElement, 0xC3D4E5F607182930);

    GEMMETHOD_(void, SetSize)(const Math::FloatVector2& size) = 0;
    GEMMETHOD_(const Math::FloatVector2&, GetSize)() const = 0;
    GEMMETHOD_(void, SetFillColor)(const Math::FloatVector4& color) = 0;
    GEMMETHOD_(const Math::FloatVector4&, GetFillColor)() const = 0;
};

//------------------------------------------------------------------------------------------------
struct XUIGraphNode : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XUIGraphNode, 0xE5F6071829304152);

    GEMMETHOD(AddChild)(_In_ XUIGraphNode* pChild) = 0;
    GEMMETHOD(RemoveChild)(_In_ XUIGraphNode* pChild) = 0;
    GEMMETHOD_(XUIGraphNode*, GetParent)() = 0;
    GEMMETHOD_(XUIGraphNode*, GetFirstChild)() = 0;
    GEMMETHOD_(XUIGraphNode*, GetNextSibling)() = 0;

    GEMMETHOD_(const Math::FloatVector2&, GetLocalPosition)() const = 0;
    GEMMETHOD_(void, SetLocalPosition)(const Math::FloatVector2& position) = 0;
    GEMMETHOD_(Math::FloatVector2, GetGlobalPosition)() = 0;

    GEMMETHOD(BindElement)(_In_ XUIElement* pElement) = 0;
    GEMMETHOD_(UINT, GetBoundElementCount)() = 0;
    GEMMETHOD_(XUIElement*, GetBoundElement)(UINT index) = 0;
};

//------------------------------------------------------------------------------------------------
struct XUIGraph : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XUIGraph, 0xD4E5F60718293041);

    GEMMETHOD_(XGfxDevice *, GetDevice)() = 0;
    GEMMETHOD(RemoveElement)(XUIElement* pElement) = 0;
    GEMMETHOD(CreateNode)(XUIGraphNode* pParent, XUIGraphNode** ppNode) = 0;
    GEMMETHOD_(XUIGraphNode*, GetRootNode)() = 0;
    GEMMETHOD(Update)() = 0;
    GEMMETHOD(SubmitRenderables)(XGfxRenderQueue* pRenderQueue) = 0;
};


//------------------------------------------------------------------------------------------------
enum TypeId : uint64_t
{
    TypeId_GfxDevice = 0xB3E4D6F1C4A7E5D2,
    TypeId_GfxBuffer = 0xD4F1A2B3C4D5E6F7,
    TypeId_GfxSurface = 0xE5F6A7B8C9D0E1F2,
};

//------------------------------------------------------------------------------------------------
struct XCanvasPlugin : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XCanvasPlugin, 0xB3E4D6F1C4A7E5D2);

    GEMMETHOD(CreateCanvasElement)(XCanvas *pCanvas, uint64_t typeId, const char *name, Gem::InterfaceId iid, void **ppElement) = 0;
};

//------------------------------------------------------------------------------------------------
// Helper classes
class CFunctionSentinel
{
    LogLevel m_DefaultLogLevel;
    XLogger *m_pLogger = nullptr;
    const char *m_FunctionName;
    Gem::Result m_Result = Gem::Result::Success;

    void LogWithLevel(LogLevel level, PCSTR format, ...) const
    {
        if (m_pLogger)
        {
            va_list args;
            va_start(args, format);
            m_pLogger->Log(level, format, args);
            va_end(args);
        }
    }

public:
    CFunctionSentinel(const char *FunctionName, XLogger *pLogger, LogLevel DefaultLogLevel = LogLevel::Info) :
        m_pLogger(pLogger),
        m_FunctionName(FunctionName),
        m_DefaultLogLevel(DefaultLogLevel)
    {
        if (pLogger)
        {
            LogWithLevel(m_DefaultLogLevel, "Begin: %s", m_FunctionName);
        }
    }

    void ReportMessage(LogLevel logLevel, const char *Format, ...) const
    {
        if (m_pLogger)
        {
            va_list args;
            va_start(args, Format);
            m_pLogger->Log(logLevel, Format, args);
            va_end(args);
        }
    }

    ~CFunctionSentinel()
    {
        if (m_pLogger) {
            LogLevel level = Gem::Failed(m_Result) ? LogLevel::Error : m_DefaultLogLevel;
            LogWithLevel(level, "%s: %s", GemResultString(m_Result), m_FunctionName);
        }
    }

    void SetResultCode(Gem::Result Result) { m_Result = Result; }
};

}

using FnCreateCanvasPlugin = Gem::Result (*)(Canvas::XCanvasPlugin**);


