//================================================================================================
// CanvasCore
//================================================================================================

#pragma once

#define CANVASAPI __stdcall
#define CANVASNOTHROW __declspec(nothrow)
#define CANVASMETHOD(method) virtual CANVASNOTHROW Canvas::Result method
#define CANVASMETHOD_(retType, method) virtual CANVASNOTHROW retType method
#define CANVASMETHODIMP Canvas::Result CANVASAPI
#define CANVASMETHODIMP_(retType) retType CANVASAPI
#define CANVAS_INTERFACE struct
#define CANVAS_INTERFACE_DECLARE(iid) static const InterfaceId IId = InterfaceId::##iid;

#define CANVAS_IID_PPV_ARGS(ppObj) \
    std::remove_reference_t<decltype(**ppObj)>::IId, reinterpret_cast<void **>(ppObj)


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
    IIterator,
    ITreeIterator,
};

enum NODE_ELEMENT_FLAGS
{
    NODE_ELEMENT_FLAGS_NONE             = 0x0,
    NODE_ELEMENT_FLAGS_CAMERA           = 0x1,
    NODE_ELEMENT_FLAGS_LIGHT            = 0x2,
    NODE_ELEMENT_FLAGS_MODELINSTANCE    = 0x4,
    NODE_ELEMENT_FLAGS_TRANSFORM        = 0x8,
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

    template<class _IFace>
    Result QueryInterface(_IFace **ppObj)
    {
        return QueryInterface(_IFace::IId, reinterpret_cast<void **>(ppObj));
    }
};

CANVAS_INTERFACE IIterator : public IGeneric
{
    CANVAS_INTERFACE_DECLARE(IIterator)
    CANVASMETHOD(MoveNext)() = 0;
    CANVASMETHOD(MovePrev)() = 0;
};

CANVAS_INTERFACE 
ITreeIterator : public IGeneric
{
    CANVAS_INTERFACE_DECLARE(ITreeIterator)

    CANVASMETHOD(MoveNext)() = 0;
    CANVASMETHOD(MovePrev)() = 0;
    CANVASMETHOD(MoveFirstChild)() = 0;
    CANVASMETHOD(MoveParent)() = 0;
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
ISceneGraphNode : public IGeneric
{
    CANVAS_INTERFACE_DECLARE(ISceneGraphNode)

    CANVASMETHOD(Insert)(_In_ ISceneGraphNode *pParent, _In_opt_ ISceneGraphNode *pInsertBefore) = 0;
    CANVASMETHOD(Remove)() = 0;
    CANVASMETHOD_(ISceneGraphNode *, GetParent)() = 0;
    CANVASMETHOD_(ISceneGraphNode *, GetFirstChild)() = 0;
    CANVASMETHOD_(ISceneGraphNode *, GetLastChild)() = 0;
    CANVASMETHOD_(ISceneGraphNode *, GetPrevSibling)() = 0;
    CANVASMETHOD_(ISceneGraphNode *, GetNextSibling)() = 0;
};

CANVAS_INTERFACE
IScene : public IGeneric
{
};

}


extern Canvas::Result CANVASAPI CreateCanvas(Canvas::InterfaceId iid, _Outptr_ void **ppCanvas);

