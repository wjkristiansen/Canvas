//================================================================================================
// Canvas
//================================================================================================

#pragma once

namespace Canvas
{
// Canvas core interfaces
GEM_INTERFACE XCanvas;

// Scene GEM_INTERFACE
GEM_INTERFACE XScene;

// Scene graph node
GEM_INTERFACE XSceneGraphNode;

// Camera GEM_INTERFACEs
GEM_INTERFACE XCamera;

// Light GEM_INTERFACEs
GEM_INTERFACE XLight;

// Transform GEM_INTERFACEs
GEM_INTERFACE XTransform;

// Assets
GEM_INTERFACE XTexture;
GEM_INTERFACE XMaterial;
GEM_INTERFACE XMesh;
GEM_INTERFACE XAmination;
GEM_INTERFACE XSkeleton;

//------------------------------------------------------------------------------------------------
inline bool Succeeded(Gem::Result result)
{
    return result < Gem::Result::Fail;
}

enum CanvasIId
{
    CanvasIId_XCanvas = 1U,
    CanvasIId_XScene = 2U,
    CanvasIId_XSceneGraphNode = 3U,
    CanvasIId_XSceneGraphIterator = 4U,
    CanvasIId_XMeshInstance = 5U,
    CanvasIId_XCamera = 6U,
    CanvasIId_XLight = 7U,
    CanvasIId_XTransform = 8U,
    CanvasIId_XTexture = 9U,
    CanvasIId_XMaterial = 10U,
    CanvasIId_XMesh = 11U,
    CanvasIId_XAmination = 12U,
    CanvasIId_XSkeleton = 13U,
    CanvasIId_XIterator = 14U,
    CanvasIId_XObjectName = 15U,
    CanvasIId_XModel = 16U,
    CanvasIId_XGraphicsDevice = 17U,
};

//------------------------------------------------------------------------------------------------
enum class ObjectType : unsigned
{
    Unknown,
    Null,
    Scene,
    SceneGraphNode,
    Transform,
    Camera,
    MeshInstance,
    Light,
};

//------------------------------------------------------------------------------------------------
enum class LightType : unsigned
{
    Null,
    Point,
    Directional,
    Spot,
    Area,
    Volume
};

//------------------------------------------------------------------------------------------------
enum class GraphicsSubsystem
{
    Null = 0,
    D3D12 = 1,
    D3D11 = 2,
    Vulcan = 3,
    Metal = 4,
};

//------------------------------------------------------------------------------------------------
struct CANVAS_GRAPHICS_OPTIONS
{
    GraphicsSubsystem Subsystem;
    bool Windowed;
    UINT DisplayWidth;
    UINT DisplayHeight;
};

//------------------------------------------------------------------------------------------------
// An indexed triangle list with common material and texture attributes
// The actual layout of pixels depends on the material
struct TRIANGLE_GROUP_DATA
{
    UINT NumTriangles = 0;
   _In_count_(NumTriangles)  UIntVector3 *pTriangles = nullptr;
};

//------------------------------------------------------------------------------------------------
struct MESH_DATA
{
    UINT NumVertices = 0;
    _In_count_(NumVertices) FloatVector3 *pVertices = nullptr;
    _In_opt_count_(NumVertices) FloatVector3 *pNormals = nullptr;
    _In_opt_count_(NumVertices) FloatVector2 *pTextureUVs[4] = {0};
    _In_opt_count_(NumVertices) UIntVector4 *pBoneIndices = nullptr;
    _In_opt_count_(NumVertices) FloatVector4 *pBoneWeights = nullptr;
    UINT NumTriangleGroups = 0;
    _In_count_(NumTriangleGroups) TRIANGLE_GROUP_DATA *pTriangleGroups = nullptr;
};

//------------------------------------------------------------------------------------------------
struct MATERIAL_DATA
{

};

//------------------------------------------------------------------------------------------------
struct TEXTURE_DATA
{

};

//------------------------------------------------------------------------------------------------
struct CAMERA_DATA
{
    float NearClip;
    float FarClip;
    float FovAngle;
};

//------------------------------------------------------------------------------------------------
struct LIGHT_DATA
{
    LightType Type;
    float Intensity;
    FloatVector4 Color;
    float InnerAngle; // For spot light
    float OuterAngle; // For spot light

};


//------------------------------------------------------------------------------------------------
GEM_INTERFACE XIterator : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XIterator);

    // Resets the iterator to the start of the collection
    GEMMETHOD(Reset)() = 0;

    // Returns true if the the iterator is at the end of the collection
    GEMMETHOD_(bool, IsAtEnd)() = 0;

    // Moves the iterator to the next element
    GEMMETHOD(MoveNext)() = 0;

    // Moves the iterator to the previous element
    GEMMETHOD(MovePrev)() = 0;

    // QI's the current element (if exists)
    GEMMETHOD(Select)(Gem::InterfaceId iid, _Outptr_ void **ppObj) = 0;
    
    // Removes the current element and the iterator to the next element
    GEMMETHOD(Prune)() = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XObjectName : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XObjectName);

    GEMMETHOD_(PCWSTR, GetName)() = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XGraphicsDevice : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XGraphicsDevice);

    GEMMETHOD(CreateMesh)(const MESH_DATA *pMeshData, XMesh **ppMesh) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XCanvas : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XCanvas);

    GEMMETHOD(CreateScene)(Gem::InterfaceId iid, _Outptr_ void **ppObj) = 0;
    GEMMETHOD(CreateObject)(ObjectType type, Gem::InterfaceId iid, _Outptr_ void **ppObj, PCWSTR szName = nullptr) = 0;
    GEMMETHOD(GetNamedObject)(_In_z_ PCWSTR szName, Gem::InterfaceId iid, _Outptr_ void **ppObj) = 0;

    GEMMETHOD(SetupGraphics)(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice) = 0;
    //GEMMETHOD(CreateLoadModelWorker)(PCWSTR szModelPath) = 0;
    GEMMETHOD(FrameTick)() = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XMaterial : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XMaterial);

    GEMMETHOD(Initialize)() = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XMesh : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XMesh);
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XMeshInstance : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XMeshInstance);

//    GEMMETHOD(SetMesh)(XMesh *pMesh) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XCamera : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XCamera);
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XLight : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XLight);
};

//------------------------------------------------------------------------------------------------
enum RotationType
{
    EulerXYZ,
    EulerXZY,
    EulerYXZ,
    EulerYZX,
    EulerZXY,
    EulerZYX,
    QuaternionWXYZ,
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XTransform : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XTransform);

    GEMMETHOD_(RotationType, GetRotationType)() const = 0;
    GEMMETHOD_(const FloatVector4 &, GetRotation)() const = 0;
    GEMMETHOD_(const FloatVector3 &, GetTranslation)() const = 0;
    GEMMETHOD_(void, SetRotation)(RotationType Type, _In_ const FloatVector4 &Rotation) = 0;
    GEMMETHOD_(void, SetTranslation)(_In_ const FloatVector3 &Translation) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XSceneGraphNode : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XSceneGraphNode);
    GEMMETHOD(AddChild)(_In_ XSceneGraphNode *pChild) = 0;
    GEMMETHOD(CreateChildIterator)(_Outptr_ XIterator **ppIterator) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XScene : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XScene);
};

}

extern Gem::Result GEMAPI CreateCanvas(Gem::InterfaceId iid, _Outptr_ void **ppCanvas);

