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
#define CANVAS_INTERFACE_DECLARE(iid) static const InterfaceId IId = IId_##iid;

#define CANVAS_IID_PPV_ARGS(ppObj) \
    std::remove_reference_t<decltype(**ppObj)>::IId, reinterpret_cast<void **>(ppObj)


namespace Canvas
{
// Generic
CANVAS_INTERFACE XGeneric;

// Canvas core interfaces
CANVAS_INTERFACE XCanvas;

// Scene CANVAS_INTERFACE
CANVAS_INTERFACE XScene;

// Scene graph node
CANVAS_INTERFACE XSceneGraphNode;

// Camera CANVAS_INTERFACEs
CANVAS_INTERFACE XCamera;

// Light CANVAS_INTERFACEs
CANVAS_INTERFACE XLight;

// Transform CANVAS_INTERFACEs
CANVAS_INTERFACE XTransform;

// Assets
CANVAS_INTERFACE XTexture;
CANVAS_INTERFACE XMaterial;
CANVAS_INTERFACE XMesh;
CANVAS_INTERFACE XAmination;
CANVAS_INTERFACE XSkeleton;

//------------------------------------------------------------------------------------------------
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

//------------------------------------------------------------------------------------------------
inline bool Failed(Result result)
{
    return result < Result::Success;
}

//------------------------------------------------------------------------------------------------
inline bool Succeeded(Result result)
{
    return result == Result::Success;
}

//------------------------------------------------------------------------------------------------
enum InterfaceIds
{
    IId_XUnknown = 0,
    IId_XGeneric = 1,
    IId_XCanvas,
    IId_XScene,
    IId_XSceneGraphNode,
    IId_XSceneGraphIterator,
    IId_XModelInstance,
    IId_XCamera,
    IId_XLight,
    IId_XTransform,
    IId_XTexture,
    IId_XMaterial,
    IId_XMesh,
    IId_XAmination,
    IId_XSkeleton,
    IId_XIterator,
    IId_XTreeIterator,

    IId_User = 0x80000000,
};

typedef UINT InterfaceId;

//------------------------------------------------------------------------------------------------
enum OBJECT_ELEMENT_FLAGS
{
    OBJECT_ELEMENT_FLAG_NONE                = 0x0,
    OBJECT_ELEMENT_FLAG_SCENE_GRAPH_NODE    = 0x1,
    OBJECT_ELEMENT_FLAG_CAMERA              = 0x2,
    OBJECT_ELEMENT_FLAG_LIGHT               = 0x4,
    OBJECT_ELEMENT_FLAG_MODELINSTANCE       = 0x10,
    OBJECT_ELEMENT_FLAG_TRANSFORM           = 0x20,
};
inline OBJECT_ELEMENT_FLAGS operator|(const OBJECT_ELEMENT_FLAGS &a, const OBJECT_ELEMENT_FLAGS &b) { return (OBJECT_ELEMENT_FLAGS)(int(a) | int(b)); }

//------------------------------------------------------------------------------------------------
struct CAMERA_DESC
{
    float NearClip;
    float FarClip;
};

//------------------------------------------------------------------------------------------------
enum LIGHT_TYPE
{
    LIGHT_TYPE_POINT,
    LIGHT_TYPE_DIRECTIONAL,
    LIGHT_TYPE_SPOT,
};

//------------------------------------------------------------------------------------------------
struct LIGHT_DESC
{
    LIGHT_TYPE Type;
};

//------------------------------------------------------------------------------------------------
struct OBJECT_DESC
{
    OBJECT_ELEMENT_FLAGS    ElementFlags;
};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE XGeneric
{
    CANVAS_INTERFACE_DECLARE(XGeneric)

    CANVASMETHOD_(ULONG, AddRef)() = 0;
    CANVASMETHOD_(ULONG, Release)() = 0;
    CANVASMETHOD(QueryInterface)(InterfaceId iid, void **ppObj) = 0;

    // Helper for typed QI
    template<class _XFace>
    Result QueryInterface(_XFace **ppObj)
    {
        return QueryInterface(_XFace::IId, reinterpret_cast<void **>(ppObj));
    }
};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE XIterator : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XIterator)
    CANVASMETHOD(MoveNext)() = 0;
    CANVASMETHOD(MovePrev)() = 0;
};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE 
XTreeIterator : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XTreeIterator)

    CANVASMETHOD(MoveNext)() = 0;
    CANVASMETHOD(MovePrev)() = 0;
    CANVASMETHOD(MoveFirstChild)() = 0;
    CANVASMETHOD(MoveParent)() = 0;
};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE
XCanvas : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XCanvas)

    CANVASMETHOD(CreateScene)(Canvas::InterfaceId iid, _Outptr_ void **ppScene) = 0;
    CANVASMETHOD(CreateNode)(PCSTR pName, OBJECT_ELEMENT_FLAGS flags, InterfaceId iid, _Outptr_ void **ppSceneGraphNode) = 0;
};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE
XModelInstance : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XModelInstance)

};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE
XCamera : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XCamera)

};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE
XLight : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XLight)

};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE
XTransform : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XTransform)

};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE
XSceneGraphIterator : public XGeneric
{
    CANVASMETHOD(GetSceneGraphNode)(InterfaceId iid, void **ppObj) = 0;
    CANVASMETHOD(MoveTo)(XSceneGraphNode *pObj) = 0;
    CANVASMETHOD(MoveParent)() = 0;
    CANVASMETHOD(MoveFirstChild)() = 0;
};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE
XSceneGraphNode : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XSceneGraphNode)

    CANVASMETHOD(Insert)(_In_ XSceneGraphNode *pParent, _In_opt_ XSceneGraphNode *pInsertBefore) = 0;
    CANVASMETHOD(Remove)() = 0;
};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE
XScene : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XScene)
};

//------------------------------------------------------------------------------------------------
// Custom interfaces must derive from CGenericBase
class CGenericBase
{
public:
    virtual ~CGenericBase() = default;

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        return Result::NoInterface;
    }
};

//------------------------------------------------------------------------------------------------
template<class _Base, InterfaceId IId>
class CInnerGeneric :
    public _Base
{
public:
    XGeneric *m_pOuterGeneric = 0; // weak pointer
    XGeneric *m_pNextInnerGeneric = nullptr;

    CInnerGeneric(_In_ XGeneric *pOuterGeneric) :
        m_pOuterGeneric(pOuterGeneric)
    {
    }

    // Forward AddRef to outer generic
    CANVASMETHOD_(ULONG,AddRef)() final
    {
        return m_pOuterGeneric->AddRef();
    }

    // Forward Release to outer generic
    CANVASMETHOD_(ULONG, Release)() final
    {
        return m_pOuterGeneric->Release();
    }

    // Forward Query interface to outer generic
    CANVASMETHOD(QueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        return m_pOuterGeneric->QueryInterface(iid, ppObj);
    }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        if (m_pNextInnerGeneric)
        {
            return m_pNextInnerGeneric->QueryInterface(iid, ppObj);
        }

        return Result::NoInterface;
    }
};

}

//------------------------------------------------------------------------------------------------
class CObjectFactory
{
    virtual Result CreateObject(InterfaceId *pInnerInterfaces, UINT numInnerInterfaces, InterfaceId iid, _Outptr_ void **ppObj);
};


extern Canvas::Result CANVASAPI CreateCanvas(Canvas::InterfaceId iid, _Outptr_ void **ppCanvas);

