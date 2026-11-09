//================================================================================================
// CanvasCore
//
// Canvas uses row vectors throughout; see "Matrix and Quaternion Conventions" in the root README.
//================================================================================================

#pragma once
#include "CanvasMath.hpp"
#include <cstdarg>
#include <cstdint>
#include <type_traits>

//------------------------------------------------------------------------------------------------
// SAL annotations.  MSVC supplies them through <sal.h>; other compilers get empty definitions
// so annotated declarations still compile.
#if defined(_MSC_VER)
  #include <sal.h>
#else
  #ifndef _In_
    #define _In_
  #endif
  #ifndef _In_opt_
    #define _In_opt_
  #endif
  #ifndef _In_z_
    #define _In_z_
  #endif
  #ifndef _Inout_
    #define _Inout_
  #endif
  #ifndef _Out_
    #define _Out_
  #endif
  #ifndef _Out_opt_
    #define _Out_opt_
  #endif
#endif

//------------------------------------------------------------------------------------------------
// Bitwise operators for an enum class whose enumerators are bit flags.  Invoke at namespace
// scope, in the enum's own namespace, immediately after its declaration.
#define CANVAS_ENUM_FLAG_OPERATORS(E) \
    constexpr E operator|(E a, E b) noexcept { return E(std::underlying_type_t<E>(a) | std::underlying_type_t<E>(b)); } \
    constexpr E operator&(E a, E b) noexcept { return E(std::underlying_type_t<E>(a) & std::underlying_type_t<E>(b)); } \
    constexpr E operator^(E a, E b) noexcept { return E(std::underlying_type_t<E>(a) ^ std::underlying_type_t<E>(b)); } \
    constexpr E operator~(E a) noexcept { return E(~std::underlying_type_t<E>(a)); } \
    constexpr E &operator|=(E &a, E b) noexcept { return a = a | b; } \
    constexpr E &operator&=(E &a, E b) noexcept { return a = a & b; } \
    constexpr E &operator^=(E &a, E b) noexcept { return a = a ^ b; }

namespace Canvas
{

// True if any bit of mask is set in flags.
template <typename E>
constexpr bool HasAnyFlag(E flags, E mask) noexcept
{
    static_assert(std::is_enum_v<E>, "HasAnyFlag requires an enum type");
    return (std::underlying_type_t<E>(flags) & std::underlying_type_t<E>(mask)) != 0;
}

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
struct XWorld;
struct XCamera;
struct XLight;
struct XGfxBuffer;
struct XGfxMeshData;
struct XGfxMaterial;
struct XMeshInstance;
struct XModel;
struct XModelBuilder;
struct XSkeletonInstance;
struct XSceneGraphNode;
struct XSceneGraphElement;
struct XAnimationController;
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
struct GfxBackgroundDesc;

//------------------------------------------------------------------------------------------------
// Light types (immutable - set at creation time)
enum class LightType : uint32_t
{
    Ambient = 0,
    Point = 1,
    Directional = 2,
    Spot = 3,
    Area = 4,
};

//------------------------------------------------------------------------------------------------
// Light flags (mutable - rendering hints)
enum class LightFlags : uint32_t
{
    None = 0,
    CastsShadows = 1 << 0,
    Enabled = 1 << 1,
};
CANVAS_ENUM_FLAG_OPERATORS(LightFlags)

//------------------------------------------------------------------------------------------------
// Log severity levels (ABI-safe, matches QLog::Level)
enum class LogLevel : uint8_t
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

    GEMMETHOD_(void, Log)(LogLevel level, const char * format, va_list args) = 0;
    GEMMETHOD_(void, SetLevel)(LogLevel level) = 0;
    GEMMETHOD_(LogLevel, GetLevel)() = 0;
    GEMMETHOD_(void, Flush)() = 0;
};

//------------------------------------------------------------------------------------------------
// XLogger helper functions

inline void LogTrace(XLogger* logger, const char * format, ...)
{
    if (logger)
    {
        va_list args;
        va_start(args, format);
        logger->Log(LogLevel::Trace, format, args);
        va_end(args);
    }
}

inline void LogDebug(XLogger* logger, const char * format, ...)
{
    if (logger)
    {
        va_list args;
        va_start(args, format);
        logger->Log(LogLevel::Debug, format, args);
        va_end(args);
    }
}

inline void LogInfo(XLogger* logger, const char * format, ...)
{
    if (logger)
    {
        va_list args;
        va_start(args, format);
        logger->Log(LogLevel::Info, format, args);
        va_end(args);
    }
}

inline void LogWarn(XLogger* logger, const char * format, ...)
{
    if (logger)
    {
        va_list args;
        va_start(args, format);
        logger->Log(LogLevel::Warn, format, args);
        va_end(args);
    }
}

inline void LogError(XLogger* logger, const char * format, ...)
{
    if (logger)
    {
        va_list args;
        va_start(args, format);
        logger->Log(LogLevel::Error, format, args);
        va_end(args);
    }
}

inline void LogCritical(XLogger* logger, const char * format, ...)
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

    GEMMETHOD(LoadPlugin)(const char * path, struct XCanvasPlugin **ppPlugin) = 0;

    GEMMETHOD(CreateWorld)(XGfxDevice *pDevice, XWorld **ppWorld, const char * name = nullptr) = 0;
    GEMMETHOD(CreateSceneGraphNode)(XSceneGraphNode **ppNode, const char * name = nullptr) = 0;
    GEMMETHOD(CreateCamera)(XCamera **ppCamera, const char * name = nullptr) = 0;
    GEMMETHOD(CreateLight)(LightType type, XLight **ppLight, const char * name = nullptr) = 0;
    GEMMETHOD(CreateMeshInstance)(XMeshInstance **ppMeshInstance, const char * name = nullptr) = 0;

    // Creates a builder that produces immutable XModel templates whose GPU resources
    // belong to pDevice.  See XModelBuilder.
    GEMMETHOD(CreateModelBuilder)(XGfxDevice *pDevice, XModelBuilder **ppBuilder, const char * name = nullptr) = 0;

    // Text/UI factory methods
    GEMMETHOD(CreateFont)(const uint8_t* pTTFData, size_t dataSize, const char * name, XFont** ppFont) = 0;
    GEMMETHOD(CreateUIGraph)(XGfxDevice* pDevice, XUIGraph** ppGraph) = 0;
    
    // Element registration methods - ONLY call from XCanvasElement::Register/Unregister implementations
    // External code should call element->Register(canvas), NOT canvas->RegisterElement(element)
    GEMMETHOD(RegisterElement)(struct XCanvasElement *) = 0;
    GEMMETHOD(UnregisterElement)(struct XCanvasElement *) = 0;

    GEMMETHOD_(XLogger *, GetLogger)() = 0;
};

//------------------------------------------------------------------------------------------------
struct
XNamedElement : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XNamedElement, 0x314EEA856A888F9D);

    GEMMETHOD_(const char *, GetName)() = 0;
    GEMMETHOD_(void, SetName)(const char * szName) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XCanvasElement : public XNamedElement
{
    GEM_INTERFACE_DECLARE(XCanvasElement, 0x5604F8425EBF3A75);

    GEMMETHOD_(XCanvas *, GetCanvas)() = 0;
    GEMMETHOD_(const char *, GetTypeName)() = 0;

    // Public API for element registration - external code should call these methods
    GEMMETHOD(Register)(XCanvas *pCanvas) = 0;
    GEMMETHOD(Unregister)() = 0;
};

//------------------------------------------------------------------------------------------------
// Sentinel for an index that refers to nothing, such as the parent of a top-level model node,
// the parent of a root bone, or a bone name that FindBone does not find.
static constexpr uint32_t InvalidIndex = UINT32_MAX;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure padded due to alignment specifier (FloatQuaternion is alignas(16))
#endif // _MSC_VER

// A transform as scale, rotation, and translation, composed as scale * rotation * translation.
struct Transform
{
    Math::FloatVector4      Scale       = { 1.0f, 1.0f, 1.0f, 0.0f };
    Math::FloatQuaternion   Rotation;                                   // identity
    Math::FloatVector4      Translation = { 0.0f, 0.0f, 0.0f, 1.0f };

    // The matrix scale * rotation * translation.
    Math::FloatMatrix4x4 ToMatrix() const
    {
        Math::FloatMatrix4x4 m = Math::QuaternionToRotationMatrix(Rotation);

        // Scale each axis row: S * R.
        for (int c = 0; c < 4; ++c)
        {
            m[0][c] *= Scale.X;
            m[1][c] *= Scale.Y;
            m[2][c] *= Scale.Z;
        }

        m[3][0] = Translation.X;
        m[3][1] = Translation.Y;
        m[3][2] = Translation.Z;
        m[3][3] = 1.0f;  // Homogeneous coordinate
        return m;
    }
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

//------------------------------------------------------------------------------------------------
// Animation descriptors - ABI-safe POD; no STL types.
// All pointer fields must remain valid for the duration of the XModelBuilder call that takes
// them, which copies the data.
//
// A model's animation targets are the values its clips animate in each instance.  A track is
// the keys of one value over a clip's time; a clip pairs each of its tracks with the target
// it drives.
//------------------------------------------------------------------------------------------------

// The layout of an animated value.  A value of N components is N consecutive floats.
enum class AnimationValueType : uint32_t
{
    Float      = 0,     // 1 component
    Float2     = 1,     // 2 components
    Float3     = 2,     // 3 components
    Float4     = 3,     // 4 components
    Quaternion = 4,     // 4 components: a unit quaternion, as (X, Y, Z, W)
};

// The number of floats in a value of the given type, or 0 for a type outside the enum.
constexpr uint32_t AnimationValueComponents(AnimationValueType type) noexcept
{
    switch (type)
    {
    case AnimationValueType::Float:      return 1;
    case AnimationValueType::Float2:     return 2;
    case AnimationValueType::Float3:     return 3;
    case AnimationValueType::Float4:     return 4;
    case AnimationValueType::Quaternion: return 4;
    default:                             return 0;
    }
}

// What an animation target is, and the value type of every type but Value.  A scene node
// target is the scene node that Instantiate creates for model node Index; a bone target is
// bone Index of the skeleton instance for skeleton SkeletonIndex.
enum class AnimationTargetType : uint32_t
{
    SceneNodeTranslation = 0,   // Float3: the local translation, a point (W = 1)
    SceneNodeRotation    = 1,   // Quaternion: the local rotation
    SceneNodeScale       = 2,   // Float3: the local scale, a vector (W = 0)
    BoneTranslation      = 3,   // Float3: the local translation, a point (W = 1)
    BoneRotation         = 4,   // Quaternion: the local rotation
    BoneScale            = 5,   // Float3: the local scale, a vector (W = 0)
    Value                = 6,   // ValueType: a value the application reads from the controller
};

// How a track's value changes between two keys.
enum class AnimationInterpolation : uint32_t
{
    Linear = 0,     // linear; spherical linear for Quaternion
    Step   = 1,     // holds each key's value until the next key
    Cubic  = 2,     // a cubic polynomial per segment, from the track's coefficients
};

// One animation target of a model.
struct AnimationTargetDesc
{
    AnimationTargetType         Type          = AnimationTargetType::SceneNodeTranslation;
    uint32_t                    SkeletonIndex = InvalidIndex;  // bone targets only
    uint32_t                    Index         = InvalidIndex;  // scene node and bone targets only
    AnimationValueType          ValueType     = AnimationValueType::Float;  // Value targets only
};

// The keys of one animated value.  Values and coefficients are packed: each takes as many
// floats as ValueType has components.  Key times are in nondecreasing order, and two keys at the
// same time make the value change instantly at that time, to the later key's value.  The value
// at a time before the first key is the first key's value, and after the last key, the last key's.
//
// A Cubic track's segment i runs from key i to key i + 1.  With u = (t - Time[i]) /
// (Time[i + 1] - Time[i]), its value is Value[i] + C1 * u + C2 * u^2 + C3 * u^3.
// pCoefficients holds C1, C2, C3 for each segment in order: KeyCount - 1 segments of three
// values each.
struct AnimationTrackDesc
{
    AnimationValueType          ValueType     = AnimationValueType::Float;
    AnimationInterpolation      Interpolation = AnimationInterpolation::Linear;
    const float*                pTimes        = nullptr;    // KeyCount entries, seconds from clip start, in order
    const float*                pValues       = nullptr;    // KeyCount values
    const float*                pCoefficients = nullptr;    // Cubic only: 3 * (KeyCount - 1) values
    uint32_t                    KeyCount      = 0;
};

// One animation clip.  Track i drives the model's animation target pTrackTargets[i], whose
// value type is the track's.  While a clip plays, each of the model's animation targets takes
// the value of the clip's track that drives it.  A target the clip has no track for takes its
// value at instantiation: a scene node's or bone's transform property, or zero for a Value
// target.
struct AnimationClipDesc
{
    const char*                 Name          = nullptr;    // null-terminated
    float                       Duration      = 0.0f;       // seconds
    const AnimationTrackDesc*   pTracks       = nullptr;    // TrackCount entries
    const uint32_t*             pTrackTargets = nullptr;    // TrackCount animation target indices
    uint32_t                    TrackCount    = 0;
};

// Sentinel returned by XAnimationController::FindClip when no clip matches the name.
static constexpr uint32_t InvalidClipIndex = UINT32_MAX;

//------------------------------------------------------------------------------------------------
// XAnimationController - a behavior that animates the content of a scene-graph subtree from
// baked animation clips.  It is NOT a spatial element: it does not describe a node's presence
// in space (no bounds, no attachment).  Instead a scene-graph node holds a typed reference to
// one via XSceneGraphNode::SetAnimationController, and the node's Update(dtime) ticks the
// controller before its spatial elements and children, so animated values are in place before
// they propagate down the hierarchy.
//
// One controller is created per XModel::Instantiate call and holds its own playback state
// (time, active clip, blend), while the heavy keyframe data is shared on the XModel.  This
// makes it cheap to have many instances of one model animating independently - e.g. a
// hillside of windmills or a hallway of zombies each at a different point in the same clip.
//
// Supports immediate clip switching and cross-fade blending.  ResetToBindPose restores the
// subtree to its instantiation-time pose (useful for editing tools and debugging).
//------------------------------------------------------------------------------------------------
struct
XAnimationController : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XAnimationController, 0xA7C3F2B841DE9506);

    // Advance playback by dtime seconds and write the resulting values onto their targets.
    // Normally called for you by the owning node's Update(); call directly only if you are
    // ticking a controller outside the scene-graph cascade.
    GEMMETHOD(Update)(float dtime) = 0;

    // Returns true if everything this controller drives is in pRoot's subtree (pRoot included).
    // Called by XSceneGraphNode::SetAnimationController before accepting the assignment.
    GEMMETHOD_(bool, ValidateForNode)(_In_ XSceneGraphNode *pRoot) = 0;

    // Query clips available from the model template (indices 0..GetClipCount()-1).
    // FindClip returns InvalidClipIndex when no clip matches the name.
    GEMMETHOD_(uint32_t, GetClipCount)() = 0;
    GEMMETHOD_(const char *,    GetClipName)(uint32_t index) = 0;
    GEMMETHOD_(uint32_t, FindClip)(const char * name) = 0;

    // Playback.  blendDuration > 0 crossfades from the current pose.  Clips loop.
    // Returns InvalidArg if clipIndex >= GetClipCount() or clipName is not found.
    GEMMETHOD(Play)(uint32_t clipIndex, float blendDuration) = 0;
    GEMMETHOD(PlayByName)(const char * clipName, float blendDuration) = 0;
    GEMMETHOD_(void, Stop)() = 0;           // freeze at current pose

    // Restore everything the controller animates to its value at instantiation time.
    GEMMETHOD_(void, ResetToBindPose)() = 0;

    // The model's animation targets, by index.  GetTargetValue writes the target's value as last
    // applied by the controller, or its value at instantiation before then, to pValue: count
    // floats, as many as the target's value type has.  Returns OutOfRange for an index out of
    // range, and InvalidArg for any other count.
    GEMMETHOD_(uint32_t, GetTargetCount)() = 0;
    GEMMETHOD(GetTargetValue)(uint32_t targetIndex, _Out_ float *pValue, uint32_t count) = 0;

    // Time / state
    GEMMETHOD_(uint32_t, GetActiveClipIndex)() = 0;
    GEMMETHOD_(float,    GetTime)() = 0;
    GEMMETHOD_(void,     SetTime)(float seconds) = 0;
    GEMMETHOD_(bool,     IsPlaying)() = 0;
};

//------------------------------------------------------------------------------------------------
enum class AnimationAttribute : uint32_t
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
    ExposureStops,
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

    GEMMETHOD_(const Transform &, GetLocalTransform)() const = 0;
    GEMMETHOD_(void, SetLocalTransform)(const Transform &transform) = 0;
    GEMMETHOD_(const Math::FloatQuaternion &, GetLocalRotation)() const = 0;
    GEMMETHOD_(const Math::FloatVector4 &, GetLocalTranslation)() const = 0;
    GEMMETHOD_(const Math::FloatVector4 &, GetLocalScale)() const = 0;
    GEMMETHOD_(void, SetLocalRotation)(const Math::FloatQuaternion &Rotation) = 0;
    GEMMETHOD_(void, SetLocalTranslation)(const Math::FloatVector4 &Translation) = 0;
    GEMMETHOD_(void, SetLocalScale)(const Math::FloatVector4 &Scale) = 0;

    GEMMETHOD_(const Math::FloatQuaternion, GetGlobalRotation)() = 0;
    GEMMETHOD_(const Math::FloatVector4, GetGlobalTranslation)() = 0;
    GEMMETHOD_(const Math::FloatMatrix4x4, GetGlobalMatrix)() = 0;
    GEMMETHOD_(const Math::FloatMatrix4x4, GetLocalMatrix)() = 0;

    // Element iteration (bound elements on this node)
    GEMMETHOD_(uint32_t, GetBoundElementCount)() = 0;
    GEMMETHOD_(XSceneGraphElement *, GetBoundElement)(uint32_t index) = 0;

    // Assign or clear (nullptr) the animation controller for this subtree.
    // Everything the controller drives must be in this node's subtree; SetAnimationController
    // calls XAnimationController::ValidateForNode and returns InvalidArg if it is not.
    GEMMETHOD(SetAnimationController)(_In_opt_ XAnimationController *pController) = 0;

    // Returns the animation controller currently driving this node's subtree, or null if none.
    GEMMETHOD_(XAnimationController *, GetAnimationController)() = 0;

    GEMMETHOD(Update)(float dtime) = 0;
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

    // Element-local axis-aligned bounding box of the element's RENDERED
    // EXTENT.
    //
    // Returned in node-local coordinates -- transformed through the
    // attached node's global matrix to obtain a world-space AABB for
    // shadow-caster aggregation, frustum culling of renderables, or
    // spatial subdivision of the scene.
    //
    // The extent here is the element's physical region in space: a
    // mesh's vertex envelope, a future particle system's
    // emitter+lifetime envelope, a decal's projector volume.
    // Non-renderable elements (XLight, XCamera, ambient) return an
    // empty AABB so they contribute nothing to renderable aggregates.
    //
    // Their AREAS OF INFLUENCE -- point/spot light attenuation volumes,
    // camera view frusta -- are a separate concept reported by
    // GetLocalInfluenceBounds so that a shadow-region or frustum-cull
    // walk can union element bounds without per-element type checks.
    GEMMETHOD_(Math::AABB, GetLocalBounds)() const = 0;

    // Element-local axis-aligned bounding box of the element's AREA OF
    // INFLUENCE: the spatial region in which this element affects
    // rendering or simulation, but does not itself occupy as a
    // renderable.
    //
    //   Point light  -> attenuation cutoff sphere -> AABB
    //   Spot light   -> attenuation + cone        -> AABB
    //   Camera       -> view frustum              -> AABB
    //   Directional / ambient light -> empty (infinite, no spatial bound)
    //   XMeshInstance               -> empty (renderable-only, no influence)
    //
    // Aggregating shadow casters and aggregating light influences are
    // distinct queries served by separate methods (GetLocalBounds vs
    // GetLocalInfluenceBounds) so neither walk has to filter the other
    // out by inspecting element type.
    GEMMETHOD_(Math::AABB, GetLocalInfluenceBounds)() const = 0;
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

    // Exposure compensation in photographic stops. 0.0 = 1x (neutral),
    // +1 doubles scene brightness, -1 halves it. Applied as a uniform
    // multiplier (exp2(stops)) in the composition / tone-map pass.
    GEMMETHOD_(void, SetExposureStops)(float stops) = 0;

    GEMMETHOD_(float, GetNearClip)() = 0;
    GEMMETHOD_(float, GetFarClip)() = 0;
    GEMMETHOD_(float, GetFovAngle)() = 0;
    GEMMETHOD_(float, GetAspectRatio)() = 0;
    GEMMETHOD_(float, GetExposureStops)() = 0;

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

    GEMMETHOD_(void, SetFlags)(LightFlags flags) = 0;
    GEMMETHOD_(LightFlags, GetFlags)() const = 0;

    // Attenuation (for Point and Spot lights)
    GEMMETHOD_(void, SetRange)(float range) = 0;
    GEMMETHOD_(float, GetRange)() const = 0;

    GEMMETHOD_(void, SetAttenuation)(float constant, float linear, float quadratic) = 0;
    GEMMETHOD_(void, GetAttenuation)(float* pConstant, float* pLinear, float* pQuadratic) const = 0;

    // Spot light parameters (only valid for Spot lights)
    GEMMETHOD_(void, SetSpotAngles)(float innerAngle, float outerAngle) = 0;
    GEMMETHOD_(void, GetSpotAngles)(float* pInnerAngle, float* pOuterAngle) const = 0;

    // Shadow parameters - take effect only when the LightFlags::CastsShadows
    // bit is set and the backend supports shadow-casting for this light type.
    // The backend allocates and manages the underlying shadow map.

    // Square shadow-map resolution in texels. 0 selects the backend default
    // (currently 2048). Higher values trade VRAM + fill cost for sharper
    // shadow edges.
    GEMMETHOD_(void, SetShadowResolution)(uint32_t pixels) = 0;
    GEMMETHOD_(uint32_t, GetShadowResolution)() const = 0;

    // Self-shadowing bias knobs.
    //   constantBias   - added to the receiver's projected depth before
    //                    the shadow compare. Use small positive values
    //                    (e.g. 1e-4) to push receivers past their caster.
    //   slopeScaleBias - scaled by max(|ddx z|, |ddy z|) during the shadow
    //                    draw to compensate steep polygons.
    //   normalOffset   - in shadow-map texels; pushes the sample point
    //                    along the surface normal at compare time so
    //                    grazing-angle surfaces don't self-shadow.
    GEMMETHOD_(void, SetShadowDepthBias)(float constantBias, float slopeScaleBias, float normalOffset) = 0;
    GEMMETHOD_(void, GetShadowDepthBias)(float* pConstantBias, float* pSlopeScaleBias, float* pNormalOffset) const = 0;

    // Shadow frustum sizing for directional lights is NOT a light-side
    // concept and so has no API here.  A directional light is infinite;
    // the "where do we care about shadows" volume is a property of the
    // view (camera frustum + shadow distance) and / or scene spatial
    // subdivision.  Until that machinery lands the backend uses a fixed
    // generous default ortho extent sufficient for current test scenes.
    // See src/CanvasGfx12/Lib/RenderQueue12.cpp BuildDirectionalShadowMatrix
    // call sites for the interim constants.
};

//------------------------------------------------------------------------------------------------
struct
XWorld : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XWorld, 0x0A470E86351AF96A);

    GEMMETHOD_(XGfxDevice *, GetDevice)() = 0;
    GEMMETHOD_(XSceneGraphNode *, GetRootNode)() = 0;

    GEMMETHOD_(void, SetActiveCamera)(XCamera *pCamera) = 0;
    GEMMETHOD_(XCamera *, GetActiveCamera)() const = 0;

    // World background (what fills G-buffer pixels with no geometry).
    // Every world has a background; the default for a newly created
    // world is opaque black.  Stored by value; any surface referenced
    // by the desc (sky cubemaps) is held strongly for as long as the
    // background references it.  Pass nullptr to reset to the default.
    GEMMETHOD_(void, SetBackground)(const GfxBackgroundDesc *pDesc) = 0;
    // Returns the current background (never nullptr).
    GEMMETHOD_(const GfxBackgroundDesc *, GetBackground)() const = 0;

    GEMMETHOD(Update)(float dtime) = 0;
    GEMMETHOD(SubmitRenderables)(XGfxRenderQueue *pRenderQueue) = 0;

    // Rebuild the world's BVH over renderable scene graph elements.  Today:
    // static-only, called explicitly after world construction / load
    // (e.g. asset bake step or end of viewer world setup).  The first
    // SubmitRenderables call also auto-builds the BVH lazily if it has
    // not been built yet, so omitting this call is correctness-safe --
    // it just shifts the build cost to frame 1.  Calling BuildBVH
    // again after scene graph topology changes is also safe; it discards any
    // prior state.
    GEMMETHOD_(void, BuildBVH)() = 0;
};

//------------------------------------------------------------------------------------------------
// XSkeletonInstance - the pose of one skeleton in one model instance.
//
// A skeleton instance is bound to a scene-graph node, the node whose space the skeleton is in:
// bone matrices are relative to that node, and a bone's world matrix is its bone matrix
// times the node's global matrix.  Bones are not scene-graph nodes.  Each bone holds a local
// transform relative to its parent bone (or, for a root bone, to the node): the position and
// rotation of its joint, the point it pivots about.  Bone matrices compose them parents first.
//
// Each socket is a scene-graph node, a child of the skeleton instance's node, with a local
// transform relative to a bone; the skeleton instance composes it with the bone's matrix to
// set the socket node's transform.  Content under a socket follows the bone.  Update(dtime), called by the node's own Update before its
// children update, recomputes the bone matrices and socket transforms when the pose changed.
// A socket node follows its bone while it is a child of the skeleton instance's node; a
// socket node removed from that node keeps its last transform.
//------------------------------------------------------------------------------------------------
struct
XSkeletonInstance : public XSceneGraphElement
{
    GEM_INTERFACE_DECLARE(XSkeletonInstance, 0x3D9A61F7C40E82B5);

    GEMMETHOD_(uint32_t, GetBoneCount)() const = 0;
    GEMMETHOD_(const char *, GetBoneName)(uint32_t index) const = 0;
    GEMMETHOD_(uint32_t, GetBoneParent)(uint32_t index) const = 0;    // InvalidIndex for a root bone
    GEMMETHOD_(uint32_t, FindBone)(const char *name) const = 0;        // InvalidIndex when absent

    // A bone's local transform, relative to its parent bone.
    // Return InvalidArg for an index out of range.
    GEMMETHOD(GetBoneLocalTransform)(uint32_t index, Transform *pTransform) const = 0;
    GEMMETHOD(SetBoneLocalTransform)(uint32_t index, const Transform *pTransform) = 0;

    // Restores every bone to the skeleton's rest transform.
    GEMMETHOD_(void, ResetToRestPose)() = 0;

    // A bone's matrix in skeleton space: relative to the skeleton instance's node.
    // Returns identity for an index out of range.
    GEMMETHOD_(Math::FloatMatrix4x4, GetBoneMatrix)(uint32_t index) = 0;
};

//------------------------------------------------------------------------------------------------
// Skin binding: connects a mesh instance to the bones of a skeleton instance.  The skin's bones
// are the skeleton bones the mesh's bone-index vertex stream references, in the skin's own
// order: skin bone i is skeleton bone pSkeletonBoneIndices[i], and pInvBindPoses[i] maps the
// mesh's geometry space to that bone's space at bind time.  BoneCount entries each; they are
// copied on SetSkinBinding.
struct SkinBindingDesc
{
    XSkeletonInstance*              pSkeleton            = nullptr;
    uint32_t                        BoneCount            = 0;
    const uint32_t*                 pSkeletonBoneIndices = nullptr;
    const Math::FloatMatrix4x4*     pInvBindPoses        = nullptr;
};

//------------------------------------------------------------------------------------------------
struct
XMeshInstance : public XSceneGraphElement
{
    GEM_INTERFACE_DECLARE(XMeshInstance, 0x1C80317FA3B1799D);

    GEMMETHOD_(XGfxMeshData *, GetMeshData)() = 0;
    GEMMETHOD_(void, SetMeshData)(XGfxMeshData *pMesh) = 0;

    // Skin binding; pass nullptr or BoneCount==0 to clear.
    GEMMETHOD(SetSkinBinding)(const SkinBindingDesc *pDesc) = 0;
    GEMMETHOD_(uint32_t, GetSkinBoneCount)() const = 0;
    GEMMETHOD_(XSkeletonInstance*, GetSkinSkeleton)() const = 0;
    GEMMETHOD_(uint32_t, GetSkinSkeletonBoneIndex)(uint32_t index) const = 0;     // InvalidIndex out of range
    GEMMETHOD_(const Math::FloatMatrix4x4*, GetSkinInvBindPose)(uint32_t index) const = 0;

    // Writes the skin palette: for each skin bone, the matrix that maps the mesh's geometry space
    // to world space in the skeleton's current pose (InvBindPose * bone matrix * the skeleton
    // node's global matrix).  count must equal GetSkinBoneCount().
    GEMMETHOD(ComputeSkinMatrices)(Math::FloatMatrix4x4 *pMatrices, uint32_t count) = 0;
};

//------------------------------------------------------------------------------------------------
// Result of model instantiation - returned by XModel::Instantiate()
struct ModelInstantiateResult
{
    XSceneGraphNode      *pInstanceRoot        = nullptr;   // Synthetic root of the instance's scene nodes
    XCamera              *pActiveCamera        = nullptr;   // The instance's active camera (null if model has none)
    XAnimationController *pAnimationController = nullptr;   // Null if model has no animation clips
};

//------------------------------------------------------------------------------------------------
// XModel - an immutable, instantiable template, produced by XModelBuilder::Build.
//
// A model holds tables, indexed from 0: model nodes, the meshes, cameras, and lights
// attached to them, skeletons, sockets, animation targets, and animation clips.  Model nodes
// are listed parents first.  Instantiate creates one scene-graph node per model node, under a
// synthetic instance root, with its elements; one skeleton instance per skeleton; and, when the
// model has clips, one animation controller.  GPU resources (mesh data, materials, textures)
// are shared by every instance.
//------------------------------------------------------------------------------------------------
struct
XModel : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XModel, 0x428769D2B64F8D29);

    // Device that owns this model's GPU resources
    GEMMETHOD_(XGfxDevice *, GetDevice)() = 0;

    // Shared GPU resources, kept alive by the model and shared across all instances.
    GEMMETHOD_(uint32_t, GetMeshDataCount)() = 0;
    GEMMETHOD_(XGfxMeshData *, GetMeshData)(uint32_t index) = 0;
    GEMMETHOD_(uint32_t, GetMaterialCount)() = 0;
    GEMMETHOD_(XGfxMaterial *, GetMaterial)(uint32_t index) = 0;
    GEMMETHOD_(uint32_t, GetTextureCount)() = 0;
    GEMMETHOD_(XGfxSurface *, GetTexture)(uint32_t index) = 0;

    // Model nodes.  GetNodeParent returns InvalidIndex for a top-level node, whose scene
    // node is a child of the instance root.  GetNodeTransform returns InvalidArg for an index
    // out of range.
    GEMMETHOD_(uint32_t, GetNodeCount)() = 0;
    GEMMETHOD_(const char *, GetNodeName)(uint32_t index) = 0;
    GEMMETHOD_(uint32_t, GetNodeParent)(uint32_t index) = 0;
    GEMMETHOD(GetNodeTransform)(uint32_t index, Transform *pTransform) = 0;

    GEMMETHOD_(uint32_t, GetSkeletonCount)() = 0;
    GEMMETHOD_(uint32_t, GetCameraCount)() = 0;
    GEMMETHOD_(uint32_t, GetActiveCameraIndex)() = 0;   // InvalidIndex when none

    GEMMETHOD_(uint32_t, GetAnimationClipCount)() = 0;
    GEMMETHOD_(const char *, GetAnimationClipName)(uint32_t index) = 0;

    // Creates an instance of the model under pTargetParent.  If pResult is non-null, it
    // receives the instance root, the instance's active camera, and the XAnimationController
    // bound to the instance root, which is null when the model has no clips.
    GEMMETHOD(Instantiate)(XSceneGraphNode *pTargetParent, ModelInstantiateResult *pResult = nullptr) = 0;

    // Axis-aligned bounding box of the model's meshes at rest, relative to the instance root.
    GEMMETHOD_(Math::AABB, GetBounds)() = 0;
};

//------------------------------------------------------------------------------------------------
// Model builder descriptors - ABI-safe POD; no STL types.  All pointer fields must remain
// valid for the duration of the call that takes them, which copies the data.  Every index
// names an entry already added to the model being built.
//------------------------------------------------------------------------------------------------

// A model node.  ParentIndex is InvalidIndex for a top-level node.
struct ModelNodeDesc
{
    const char*         Name        = nullptr;
    uint32_t            ParentIndex = InvalidIndex;
    Transform           LocalTransform;
};

// A mesh on a model node: mesh data the model holds, placed on the node.  A skinned mesh
// names a skeleton and binds its BoneCount skin bones to the skeleton's bones (see
// SkinBindingDesc).
struct ModelMeshDesc
{
    const char*                     Name                 = nullptr;
    uint32_t                        NodeIndex            = InvalidIndex;
    uint32_t                        MeshDataIndex        = InvalidIndex;
    uint32_t                        SkeletonIndex        = InvalidIndex;  // InvalidIndex: unskinned
    uint32_t                        BoneCount            = 0;
    const uint32_t*                 pSkeletonBoneIndices = nullptr;
    const Math::FloatMatrix4x4*     pInvBindPoses        = nullptr;
};

// The settable properties of an XCamera.  Defaults match XCanvas::CreateCamera.
struct CameraParams
{
    float           NearClip      = 0.1f;
    float           FarClip       = 1000.0f;
    float           FovAngle      = static_cast<float>(Math::Pi / 4);
    float           AspectRatio   = 16.0f / 9.0f;
    float           ExposureStops = 0.0f;
};

// A camera on a model node.
struct ModelCameraDesc
{
    const char*     Name      = nullptr;
    uint32_t        NodeIndex = InvalidIndex;
    CameraParams    Params;
};

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure padded due to alignment specifier (FloatVector4 is alignas(16))
#endif // _MSC_VER

// The settable properties of an XLight.  Defaults match XCanvas::CreateLight.
struct LightParams
{
    Math::FloatVector4  Color                = { 1.0f, 1.0f, 1.0f, 1.0f };
    float               Intensity            = 1.0f;
    LightFlags          Flags                = LightFlags::Enabled;
    float               Range                = 100.0f;
    float               AttenuationConstant  = 1.0f;
    float               AttenuationLinear    = 0.0f;
    float               AttenuationQuadratic = 0.0f;
    float               SpotInnerAngle       = 0.785398f;  // radians
    float               SpotOuterAngle       = 1.047198f;  // radians
};

// A light on a model node.
struct ModelLightDesc
{
    const char*         Name      = nullptr;
    uint32_t            NodeIndex = InvalidIndex;
    LightType           Type      = LightType::Point;
    LightParams         Params;
};

// A skeleton bone.  ParentIndex is a bone index in the same skeleton, less than the bone's
// own index, or InvalidIndex for a root bone.  RestTransform is relative to the parent
// bone, or for a root bone to the skeleton's node.
struct ModelBoneDesc
{
    const char*         Name          = nullptr;
    uint32_t            ParentIndex   = InvalidIndex;
    Transform           RestTransform;
};

// A skeleton, in the space of a model node, whose scene node holds each instance's skeleton
// instance.  NodeIndex InvalidIndex puts the skeleton in the space of the instance root.
struct ModelSkeletonDesc
{
    const char*             Name       = nullptr;
    uint32_t                NodeIndex  = InvalidIndex;
    uint32_t                BoneCount  = 0;
    const ModelBoneDesc*    pBones     = nullptr;
};

// A socket: a model node, a child of its skeleton's node, whose transform follows a bone.
// LocalTransform is relative to the bone.  Like every Canvas transform, a socket's transform is
// a translation, rotation, and scale, with no shear: it composes the rotations and the scales of
// the bone and its ancestors separately.
struct ModelSocketDesc
{
    const char*         Name          = nullptr;
    uint32_t            SkeletonIndex = InvalidIndex;
    uint32_t            BoneIndex     = InvalidIndex;
    Transform           LocalTransform;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

//------------------------------------------------------------------------------------------------
// XModelBuilder - builds immutable XModel templates.
//
// A builder is Uninitialized when created, after a successful Build, and after Reset.  Begin
// starts a model and puts the builder in the Building state, where the Add methods,
// SetActiveCamera, and Build are available.  In the Uninitialized state they return
// Gem::Result::Uninitialized, and Begin while Building returns Gem::Result::AlreadyExists.
//
// A successful Build moves the builder's content into a new XModel and returns the builder to
// Uninitialized, so one builder can build any number of models, one at a time.  A failed Build
// leaves the content in place.  A model has at least one model node.
//
// Each Add method validates its descriptor and, on success, writes the new entry's index to
// *pIndex when pIndex is non-null.  A rejected call adds nothing.
//------------------------------------------------------------------------------------------------
struct
XModelBuilder : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XModelBuilder, 0x6B1E93D4A27C58F1);

    // Device whose GPU resources the models are built from
    GEMMETHOD_(XGfxDevice *, GetDevice)() = 0;

    // Starts a model.  name (optional) becomes the XModel's name.
    GEMMETHOD(Begin)(const char *name) = 0;

    // Discards the content and returns the builder to Uninitialized.
    GEMMETHOD_(void, Reset)() = 0;

    GEMMETHOD_(bool, IsBuilding)() const = 0;

    // Shared GPU resources.  Meshes name mesh data by its index.
    GEMMETHOD(AddMeshData)(_In_ XGfxMeshData *pMeshData, _Out_opt_ uint32_t *pIndex) = 0;
    GEMMETHOD(AddMaterial)(_In_ XGfxMaterial *pMaterial, _Out_opt_ uint32_t *pIndex) = 0;
    GEMMETHOD(AddTexture)(_In_ XGfxSurface *pTexture, _Out_opt_ uint32_t *pIndex) = 0;

    GEMMETHOD(AddNode)(_In_ const ModelNodeDesc *pDesc, _Out_opt_ uint32_t *pIndex) = 0;
    GEMMETHOD(AddMesh)(_In_ const ModelMeshDesc *pDesc, _Out_opt_ uint32_t *pIndex) = 0;
    GEMMETHOD(AddCamera)(_In_ const ModelCameraDesc *pDesc, _Out_opt_ uint32_t *pIndex) = 0;
    GEMMETHOD(AddLight)(_In_ const ModelLightDesc *pDesc, _Out_opt_ uint32_t *pIndex) = 0;
    GEMMETHOD(AddSkeleton)(_In_ const ModelSkeletonDesc *pDesc, _Out_opt_ uint32_t *pIndex) = 0;

    // Adds a socket's model node and writes the node's index to *pNodeIndex.
    GEMMETHOD(AddSocket)(_In_ const ModelSocketDesc *pDesc, _Out_opt_ uint32_t *pNodeIndex) = 0;

    // Animation targets and clips are shared across all instances.  AddAnimationTarget returns
    // AlreadyExists for a scene node or bone target of the same type and transform as an
    // earlier one.  A clip's targets are added before the clip; AddAnimationClip
    // returns OutOfRange for a track target that does not exist yet, and InvalidArg for a track
    // whose value type differs from its target's, a track whose key times are out of order, or a
    // target driven by more than one track.
    // Model node, skeleton, and bone indices of targets are checked by Build.
    GEMMETHOD(AddAnimationTarget)(_In_ const AnimationTargetDesc *pDesc, _Out_opt_ uint32_t *pIndex) = 0;
    GEMMETHOD(AddAnimationClip)(_In_ const AnimationClipDesc *pDesc, _Out_opt_ uint32_t *pIndex) = 0;

    // Designates the camera that Instantiate reports as the instance's active camera.
    GEMMETHOD(SetActiveCamera)(uint32_t cameraIndex) = 0;

    GEMMETHOD(Build)(_Out_ XModel **ppModel) = 0;
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

    // Node attachment (managed by XUIGraphNode::BindElement)
    GEMMETHOD_(XUIGraphNode*, GetAttachedNode)() = 0;
    GEMMETHOD(Detach)() = 0;
    GEMMETHOD(NotifyNodeContextChanged)(_In_ XUIGraphNode* pNode) = 0;

    // Signed 2D offset relative to the parent UIGraphNode
    GEMMETHOD_(const Math::FloatVector2&, GetLocalOffset)() const = 0;
    GEMMETHOD_(void, SetLocalOffset)(const Math::FloatVector2& offset) = 0;

    // CPU-side update (regenerate cached data if dirty)
    GEMMETHOD(Update)() = 0;
    GEMMETHOD_(bool, HasContent)() const = 0;
};

//------------------------------------------------------------------------------------------------
struct XUITextElement : public XUIElement
{
    GEM_INTERFACE_DECLARE(XUITextElement, 0xB2C3D4E5F6071829);

    GEMMETHOD_(void, SetText)(const char * utf8Text) = 0;
    GEMMETHOD_(const char *, GetText)() const = 0;
    GEMMETHOD_(void, SetFont)(XFont* pFont) = 0;
    GEMMETHOD_(void, SetLayoutConfig)(const TextLayoutConfig& config) = 0;
    GEMMETHOD_(const TextLayoutConfig&, GetLayoutConfig)() const = 0;
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
    GEMMETHOD_(uint32_t, GetBoundElementCount)() = 0;
    GEMMETHOD_(XUIElement*, GetBoundElement)(uint32_t index) = 0;
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

    void LogWithLevel(LogLevel level, const char * format, ...) const
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



