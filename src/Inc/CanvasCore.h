//================================================================================================
// CanvasCore
//
// IMPORTANT: Canvas uses ROW-VECTOR matrix convention throughout!
//
// - Vectors are row vectors: v' = v * M (not M * v)
// - Translation is stored in the BOTTOM ROW: matrix[3][0], matrix[3][1], matrix[3][2]
// - Matrix concatenation: child_to_world = child_to_parent * parent_to_world
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
struct XScene;
struct XCamera;
struct XLight;
struct XGfxBuffer;
struct XGfxMeshData;
struct XGfxMaterial;
struct XMeshInstance;
struct XModel;
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

    GEMMETHOD(CreateScene)(XGfxDevice *pDevice, XScene **ppScene, PCSTR name = nullptr) = 0;
    GEMMETHOD(CreateSceneGraphNode)(XSceneGraphNode **ppNode, PCSTR name = nullptr) = 0;
    GEMMETHOD(CreateCamera)(XCamera **ppCamera, PCSTR name = nullptr) = 0;
    GEMMETHOD(CreateLight)(LightType type, XLight **ppLight, PCSTR name = nullptr) = 0;
    GEMMETHOD(CreateMeshInstance)(XMeshInstance **ppMeshInstance, PCSTR name = nullptr) = 0;
    GEMMETHOD(CreateModel)(XGfxDevice *pDevice, XModel **ppModel, PCSTR name = nullptr) = 0;

    // Text/UI factory methods
    GEMMETHOD(CreateFont)(const uint8_t* pTTFData, size_t dataSize, PCSTR name, XFont** ppFont) = 0;
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
// Animation clip descriptors - ABI-safe POD; no STL types.
// All pointer fields must remain valid for the duration of the AddAnimationClip call.
// CModel copies the data into its internal storage immediately.
//------------------------------------------------------------------------------------------------

#pragma warning(push)
#pragma warning(disable: 4324) // structure padded due to alignment specifier (FloatQuaternion is alignas(16))
// One TRS keyframe. Rotation is always stored as a unit quaternion.
struct AnimationKeyframe
{
    float                   Time;           // seconds from clip start
    Math::FloatVector4      Translation;    // W = 0
    Math::FloatQuaternion   Rotation;       // unit quaternion (Canvas space)
    Math::FloatVector4      Scale;          // W = 0
};
#pragma warning(pop)

// All keyframes for one node's track within a clip.
// NodeName is matched against the cloned XSceneGraphNode names in XModel::Instantiate.
struct AnimationTrackDesc
{
    const char*                 NodeName;       // null-terminated
    const AnimationKeyframe*    Keyframes;      // KeyframeCount entries, sorted ascending by Time
    uint32_t                    KeyframeCount;
};

// One animation clip (== one FBX AnimationStack / Blender Action).
struct AnimationClipDesc
{
    const char*                 Name;           // null-terminated
    float                       Duration;       // seconds
    const AnimationTrackDesc*   Tracks;         // TrackCount entries
    uint32_t                    TrackCount;
};

// Sentinel returned by XAnimationController::FindClip when no clip matches the name.
static constexpr uint32_t InvalidClipIndex = UINT32_MAX;

//------------------------------------------------------------------------------------------------
// XAnimationController - a behavior that drives the transforms of a subtree of scene-graph
// nodes from baked animation clips.  It is NOT a spatial element: it does not describe a
// node's presence in space (no bounds, no attachment).  Instead a scene-graph node holds
// a typed reference to one via XSceneGraphNode::SetAnimationController, and the node's
// Update(dtime) ticks the controller before its spatial elements and children, so animated
// transforms are in place before they propagate down the hierarchy.
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

    // Advance playback by dtime seconds and write the resulting pose onto the driven nodes.
    // Normally called for you by the owning node's Update(); call directly only if you are
    // ticking a controller outside the scene-graph cascade.
    GEMMETHOD(Update)(float dtime) = 0;

    // Returns true if every node this controller drives is a descendant of pRoot (or IS pRoot).
    // Called by XSceneGraphNode::SetAnimationController before accepting the assignment.
    GEMMETHOD_(bool, ValidateForNode)(_In_ XSceneGraphNode *pRoot) = 0;

    // Query clips available from the model template (indices 0..GetClipCount()-1).
    // FindClip returns InvalidClipIndex when no clip matches the name.
    GEMMETHOD_(uint32_t, GetClipCount)() = 0;
    GEMMETHOD_(PCSTR,    GetClipName)(uint32_t index) = 0;
    GEMMETHOD_(uint32_t, FindClip)(PCSTR name) = 0;

    // Playback.  blendDuration > 0 crossfades from the current pose.  Clips loop.
    // Returns InvalidArg if clipIndex >= GetClipCount() or clipName is not found.
    GEMMETHOD(Play)(uint32_t clipIndex, float blendDuration) = 0;
    GEMMETHOD(PlayByName)(PCSTR clipName, float blendDuration) = 0;
    GEMMETHOD_(void, Stop)() = 0;           // freeze at current pose

    // Restore every animated node to its bind-pose TRS (the pose at instantiation time).
    GEMMETHOD_(void, ResetToBindPose)() = 0;

    // Time / state
    GEMMETHOD_(uint32_t, GetActiveClipIndex)() = 0;
    GEMMETHOD_(float,    GetTime)() = 0;
    GEMMETHOD_(void,     SetTime)(float seconds) = 0;
    GEMMETHOD_(bool,     IsPlaying)() = 0;
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

    // Assign or clear (nullptr) the animation controller for this subtree.
    // The controller's driven nodes must all be descendants of this node; SetAnimationController
    // calls XAnimationController::ValidateForNode and returns InvalidArg if they are not.
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
    // mesh's vertex envelope, a model's union-of-meshes envelope, a
    // future particle system's emitter+lifetime envelope, a decal's
    // projector volume.  Non-renderable elements (XLight, XCamera,
    // ambient) return an empty AABB so they contribute nothing to
    // renderable aggregates.
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
    //   XMeshInstance / XModel      -> empty (renderable-only, no influence)
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

    // Shadow parameters - take effect only when the LightFlags::CastsShadows
    // bit is set and the backend supports shadow-casting for this light type.
    // The backend allocates and manages the underlying shadow map.

    // Square shadow-map resolution in texels. 0 selects the backend default
    // (currently 2048). Higher values trade VRAM + fill cost for sharper
    // shadow edges.
    GEMMETHOD_(void, SetShadowResolution)(UINT pixels) = 0;
    GEMMETHOD_(UINT, GetShadowResolution)() const = 0;

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
XScene : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XScene, 0x0A470E86351AF96A);

    GEMMETHOD_(XGfxDevice *, GetDevice)() = 0;
    GEMMETHOD_(XSceneGraphNode *, GetRootNode)() = 0;

    GEMMETHOD_(void, SetActiveCamera)(XCamera *pCamera) = 0;
    GEMMETHOD_(XCamera *, GetActiveCamera)() const = 0;

    // Scene background (what fills G-buffer pixels with no geometry).
    // Every scene has a background; the default for a newly created
    // scene is opaque black.  Stored by value; any surface referenced
    // by the desc (sky cubemaps) is held strongly for as long as the
    // background references it.  Pass nullptr to reset to the default.
    GEMMETHOD_(void, SetBackground)(const GfxBackgroundDesc *pDesc) = 0;
    // Returns the current background (never nullptr).
    GEMMETHOD_(const GfxBackgroundDesc *, GetBackground)() const = 0;

    GEMMETHOD(Update)(float dtime) = 0;
    GEMMETHOD(SubmitRenderables)(XGfxRenderQueue *pRenderQueue) = 0;

    // Rebuild the scene's BVH over renderable scene elements.  Today:
    // static-only, called explicitly after scene construction / load
    // (e.g. asset bake step or end of viewer scene-setup).  The first
    // SubmitRenderables call also auto-builds the BVH lazily if it has
    // not been built yet, so omitting this call is correctness-safe --
    // it just shifts the build cost to frame 1.  Calling BuildBVH
    // again after scene topology changes is also safe; it discards any
    // prior state.
    GEMMETHOD_(void, BuildBVH)() = 0;
};

//------------------------------------------------------------------------------------------------
// Skin binding: connects a mesh instance to its animated bone nodes + inverse bind poses.
// ppBoneNodes / pInvBindPoses are BoneCount entries; they are copied on SetSkinBinding.
struct SkinBindingDesc
{
    uint32_t                        BoneCount     = 0;
    XSceneGraphNode* const*         ppBoneNodes   = nullptr;
    const Math::FloatMatrix4x4*     pInvBindPoses = nullptr;
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
    GEMMETHOD_(XSceneGraphNode*, GetSkinBoneNode)(uint32_t index) const = 0;
    GEMMETHOD_(const Math::FloatMatrix4x4*, GetSkinInvBindPose)(uint32_t index) const = 0;
};

//------------------------------------------------------------------------------------------------
// Result of model instantiation - returned by XModel::Instantiate()
struct ModelInstantiateResult
{
    XSceneGraphNode      *pInstanceRoot        = nullptr;   // Synthetic root of the cloned subtree
    XCamera              *pActiveCamera        = nullptr;   // Cloned active camera (null if model has none)
    XAnimationController *pAnimationController = nullptr;   // Null if model has no animation clips
};

//------------------------------------------------------------------------------------------------
struct
XModel : public XCanvasElement
{
    GEM_INTERFACE_DECLARE(XModel, 0x428769D2B64F8D29);

    // Device that owns this model's GPU resources
    GEMMETHOD_(XGfxDevice *, GetDevice)() = 0;

    // Node hierarchy authoring - the model's internal root node.
    // Build the model by creating nodes/elements via XCanvas and
    // adding them as children of this root.
    GEMMETHOD_(XSceneGraphNode *, GetRootNode)() = 0;

    // Shared GPU resource library - resources added here are kept
    // alive by the model and shared across all instances.
    GEMMETHOD_(uint32_t, GetMeshDataCount)() = 0;
    GEMMETHOD_(XGfxMeshData *, GetMeshData)(uint32_t index) = 0;
    GEMMETHOD(AddMeshData)(XGfxMeshData *pMeshData) = 0;

    GEMMETHOD_(uint32_t, GetMaterialCount)() = 0;
    GEMMETHOD_(XGfxMaterial *, GetMaterial)(uint32_t index) = 0;
    GEMMETHOD(AddMaterial)(XGfxMaterial *pMaterial) = 0;

    GEMMETHOD_(uint32_t, GetTextureCount)() = 0;
    GEMMETHOD_(XGfxSurface *, GetTexture)(uint32_t index) = 0;
    GEMMETHOD(AddTexture)(XGfxSurface *pTexture) = 0;

    // Active camera designation - set to the node in the model's
    // hierarchy that carries the "default" camera.  Instantiate()
    // will map it to the cloned counterpart in the result.
    GEMMETHOD_(void, SetActiveCameraNode)(XSceneGraphNode *pNode) = 0;
    GEMMETHOD_(XSceneGraphNode *, GetActiveCameraNode)() = 0;

    // Animation clip library - clips added here are shared across all instances.
    // Each clip corresponds to one FBX AnimationStack (Blender Action).
    GEMMETHOD(AddAnimationClip)(const AnimationClipDesc* pClip) = 0;

    // Skin binding registration - called by BuildModel for each skinned mesh.
    // pMeshNode is the model-template node carrying the skinned XMeshInstance.
    // ppBoneNodes / pInvBindPoses are BoneCount entries (copied internally).
    // On Instantiate(), bone nodes are resolved via the clone map and
    // SetSkinBinding is called on the cloned XMeshInstance.
    struct MeshSkinDesc
    {
        XSceneGraphNode*            pMeshNode     = nullptr;
        uint32_t                    BoneCount     = 0;
        XSceneGraphNode* const*     ppBoneNodes   = nullptr;
        const Math::FloatMatrix4x4* pInvBindPoses = nullptr;
    };
    GEMMETHOD(AddMeshSkin)(const MeshSkinDesc *pDesc) = 0;
    GEMMETHOD_(uint32_t, GetAnimationClipCount)() = 0;
    GEMMETHOD_(PCSTR,    GetAnimationClipName)(uint32_t index) = 0;

    // Instantiate: deep-clone the model's node hierarchy into the
    // target scene graph.  A synthetic instance root is always
    // created under pTargetParent.  If pResult is non-null, it
    // receives the instance root, the cloned active camera, and
    // (when clips are present) an XAnimationController bound to the root.
    GEMMETHOD(Instantiate)(XSceneGraphNode *pTargetParent, ModelInstantiateResult *pResult = nullptr) = 0;

    // Axis-aligned bounding box of the model template
    GEMMETHOD_(Math::AABB, GetBounds)() = 0;
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

    GEMMETHOD_(void, SetText)(PCSTR utf8Text) = 0;
    GEMMETHOD_(PCSTR, GetText)() const = 0;
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



