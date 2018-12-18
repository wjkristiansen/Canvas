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
    CanvasIId_XModelInstance = 5U,
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
    ModelInstance,
    Light,
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
XCanvas : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XCanvas);

    GEMMETHOD(CreateScene)(Gem::InterfaceId iid, _Outptr_ void **ppObj) = 0;
    GEMMETHOD(CreateObject)(ObjectType type, Gem::InterfaceId iid, _Outptr_ void **ppObj, PCWSTR szName = nullptr) = 0;
    GEMMETHOD(GetNamedObject)(_In_z_ PCWSTR szName, Gem::InterfaceId iid, _Outptr_ void **ppObj) = 0;

    GEMMETHOD(SetupGraphics)(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XModelInstance : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XModelInstance);
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
GEM_INTERFACE
XTransform : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XTransform);
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

