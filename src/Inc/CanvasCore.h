//================================================================================================
// CanvasCore
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
template<class _T>
class TStaticPtr
{
    _T *m_ptr = nullptr;
public:
    TStaticPtr() = default;
    TStaticPtr(_T *p) :
        m_ptr(p) {}

    _T *Ptr() { return m_ptr; }
    const _T *Ptr() const { return m_ptr; }
};

//------------------------------------------------------------------------------------------------
// Node in a linked list using a sentinal node.
// By default the node points back to itself.  
// During destruction, the node removes itself from a list.
// Sentinel nodes are used to indicate the list terminator.
// In a sentinel node, m_pPrev points to the end of the list
// and m_pNext points to the beginning.
template<class _Base>
class TAutoListNode : public _Base
{
    TAutoListNode *m_pPrev;
    TAutoListNode *m_pNext;
    
public:
    template<typename ... Args>
    TAutoListNode(TAutoListNode *pPrev, Args... args) :
        _Base(args...),
        m_pPrev(this),
        m_pNext(this)
    {
        if (pPrev)
        {
            m_pPrev = pPrev;
            m_pNext = pPrev->m_pNext;
            pPrev->m_pNext->m_pPrev = this;
            pPrev->m_pNext = this;
        }
    }
    ~TAutoListNode()
    {
        // Remove this from the list
        Remove();
    }

    void Remove()
    {
        // Can't remove the sentinel node
        if (m_pNext != this)
        {
            m_pPrev->m_pNext = m_pNext;
            m_pNext->m_pPrev = m_pPrev;

            // Point to self
            m_pNext = this;
            m_pPrev = this;
        }
    }

    TAutoListNode *GetPrev() const { return m_pPrev; }
    TAutoListNode *GetNext() const { return m_pNext; }
};

//------------------------------------------------------------------------------------------------
template<class _NodeBaseType>
class TAutoList
{
    TAutoListNode<_NodeBaseType> m_Sentinel;

public:
    TAutoList() :
        m_Sentinel(nullptr) {}
    const TAutoListNode<_NodeBaseType> *GetEnd() const { return &m_Sentinel; }
    TAutoListNode<_NodeBaseType> *GetFirst() { return m_Sentinel.GetNext(); }
    TAutoListNode<_NodeBaseType> *GetLast() { return m_Sentinel.GetPrev(); }
};

//------------------------------------------------------------------------------------------------
// Derive from this class to implement a custom logger
// 
// Custom implementations are reponsibile for thread safety.
class CLogger
{
    UINT m_OutputLevel = 0UL - 1;

public:
    CLogger() = default;

    // Writes a log entry string to a logging output.  
    //
    // Level is an indication of criticallty, 0 being most-critical.
    // Typically, Canvas log entries are considered to be:
    // 0 - Critical errors or events
    // 1 - Warnings or important messages
    // 2 - Informational
    // 3 - Verbose
    // 
    // Override this method to customize overall log entry handling.
    virtual void WriteToLog(UINT Level, _In_z_ PCWSTR szString) = 0;
    
    // Limits log output to the indicated level.
    // Log entries with a higher Level are filtered out
    void SetLogOutputLevel(UINT Level) { m_OutputLevel = Level; };
    UINT GetLogOutputLevel() const { return m_OutputLevel; }

protected:
    // Override this method to provide custom logging output
    // without impacting the output level or handling of newlines.
    virtual void OutputString(_In_z_ PCWSTR szString) = 0;
};

//------------------------------------------------------------------------------------------------
// An indexed triangle list with common material and texture attributes
// The actual layout of pixels depends on the material
struct MATERIAL_GROUP_DATA
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
    UINT NumMaterialGroups = 0;
    _In_count_(NumMaterialGroups) MATERIAL_GROUP_DATA *pMaterialGroups = nullptr;
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
    GEMMETHOD(CreateCamera)(const CAMERA_DATA *pCameraData, XCamera **ppCamera) = 0;
    GEMMETHOD(CreateMaterial)(const MATERIAL_DATA *pMaterialData, XMaterial **ppMaterial) = 0;
    GEMMETHOD(CreateLight)(const LIGHT_DATA *pLightData, XLight **ppLight) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XCanvas : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XCanvas);

    GEMMETHOD(CreateScene)(Gem::InterfaceId iid, _Outptr_ void **ppObj) = 0;
    GEMMETHOD(CreateSceneGraphNode)(Gem::InterfaceId iid, _Outptr_ void **ppObj, PCWSTR szName = nullptr) = 0;
    GEMMETHOD(GetNamedObject)(_In_z_ PCWSTR szName, Gem::InterfaceId iid, _Outptr_ void **ppObj) = 0;

    GEMMETHOD(CreateGraphicsDevice)(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice) = 0;
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

    GEMMETHOD_(void, SetMesh)(XMesh *pMesh) = 0;
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

    //GEMMETHOD_(void, SetMesh)(XMesh *pMesh) = 0;
    //GEMMETHOD_(void, SetCamera)(XCamera *pCamera) = 0;
    //GEMMETHOD_(void, SetLight)(XLight *pLight) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XScene : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XScene);

    GEMMETHOD(GetRootSceneGraphNode)(Gem::InterfaceId iid, _Outptr_ void **ppObj) = 0;
};

}

extern Gem::Result GEMAPI CreateCanvas(Gem::InterfaceId iid, _Outptr_ void **ppCanvas, _In_ Canvas::CLogger *pLogger = nullptr);

