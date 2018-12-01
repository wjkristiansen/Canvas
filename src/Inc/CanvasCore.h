//================================================================================================
// CanvasMath
//================================================================================================

#pragma once

#define CANVASAPI __stdcall
#define CANVASNOTHROW __declspec(nothrow)
#define CANVASMETHOD(method) virtual Canvas::Result CANVASNOTHROW CANVASAPI method
#define CANVASMETHOD_(retType, method) virtual retType CANVASNOTHROW CANVASAPI method
#define CANVASMETHODIMP Canvas::Result CANVASAPI
#define CANVASMETHODIMP_(retType) retType CANVASAPI
#define CANVAS_INTERFACE struct
#define CANVAS_INTERFACE_DECLARE(iid) static const InterfaceId IId = InterfaceId::##iid;

namespace Canvas
{
enum class Result : int
{
    Success = 0,
    Finished = 1,
    InvalidArg = -1,
    NotFound = -2,
    OutOfMemory = -3,
    NoInterface = -4,
    BadPointer = -5,
    NotImplemented = -6,
    DuplicateKey = -7,
    Uninitialized = -8,
};

inline bool Failed(Result result)
{
    return result < Result::Success;
}

inline bool Succeeded(Result result)
{
    return result == Result::Success;
}

enum class InterfaceId : int
{
    IUnknown = 0,
    IGeneric = 1,
    ICanvas,
    IScene,
    ISceneGraphNode,
    ISceneGraphIterator,
    IModelInstance,
    ICamera,
    ILight,
    ITransform,
    ITexture,
    IMaterial,
    IMesh,
    IAnimation,
    ISkeleton,
};

enum NODE_ELEMENT_FLAGS
{
    NODE_ELEMENT_FLAGS_NONE        = 0x0,
    NODE_ELEMENT_FLAGS_CAMERA      = 0x1,
    NODE_ELEMENT_FLAGS_LIGHT       = 0x2,
    NODE_ELEMENT_FLAGS_MODEL       = 0x4,
    NODE_ELEMENT_FLAGS_TRANSFORM   = 0x8,
};

// Generic
CANVAS_INTERFACE IGeneric;

// Canvas core interfaces
CANVAS_INTERFACE ICanvas;

// Scene CANVAS_INTERFACE
CANVAS_INTERFACE IScene;

// Scene object
CANVAS_INTERFACE ISceneGraphNode;

// Camera CANVAS_INTERFACEs
CANVAS_INTERFACE ICamera;

// Light CANVAS_INTERFACEs
CANVAS_INTERFACE ILight;

// Transform CANVAS_INTERFACEs
CANVAS_INTERFACE ITransform;

// Assets
CANVAS_INTERFACE ITexture;
CANVAS_INTERFACE IMaterial;
CANVAS_INTERFACE IMesh;
CANVAS_INTERFACE IAnimation;
CANVAS_INTERFACE ISkeleton;

CANVAS_INTERFACE IGeneric
{
    CANVAS_INTERFACE_DECLARE(IGeneric)

    CANVASMETHOD_(ULONG, AddRef)() = 0;
    CANVASMETHOD_(ULONG, Release)() = 0;
    CANVASMETHOD(QueryInterface)(InterfaceId iid, void **ppObj) = 0;
};

CANVAS_INTERFACE
ICanvas : public IGeneric
{
    CANVAS_INTERFACE_DECLARE(ICanvas)

    CANVASMETHOD(CreateScene)(Canvas::InterfaceId iid, _Outptr_ void **ppScene) = 0;
    CANVASMETHOD(CreateNode)(PCSTR pName, NODE_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppSceneGraphNode) = 0;
};

CANVAS_INTERFACE
IModelInstance : public IGeneric
{
    CANVAS_INTERFACE_DECLARE(IModelInstance)

};

CANVAS_INTERFACE
ICamera : public IGeneric
{
    CANVAS_INTERFACE_DECLARE(ICamera)

};

CANVAS_INTERFACE
ILight : public IGeneric
{
    CANVAS_INTERFACE_DECLARE(ILight)

};

CANVAS_INTERFACE
ITransform : public IGeneric
{
    CANVAS_INTERFACE_DECLARE(ITransform)

};

CANVAS_INTERFACE
ISceneGraphIterator : public IGeneric
{
    CANVAS_INTERFACE_DECLARE(ISceneGraphIterator)
    CANVASMETHOD(MoveNextSibling)() = 0;
    CANVASMETHOD(GetNode(InterfaceId iid, void **ppNode)) = 0;
    CANVASMETHOD(Reset)(_In_ ISceneGraphNode *pParentNode, _In_opt_ PCSTR pName) = 0;
};

CANVAS_INTERFACE
ISceneGraphNode : public IGeneric
{
    CANVASMETHOD(AddChild)(_In_ PCSTR pName, _In_ ISceneGraphNode *pSceneNode) = 0;
};

CANVAS_INTERFACE
IScene : public IGeneric
{
};

}


extern Canvas::Result CANVASAPI CreateCanvas(Canvas::InterfaceId iid, _Outptr_ void **ppCanvas);

