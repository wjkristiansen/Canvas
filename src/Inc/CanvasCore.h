//================================================================================================
// CanvasCore
//================================================================================================

#pragma once

#define CANVASAPI __stdcall
#define CANVASNOTHROW __declspec(nothrow)
#define CANVASMETHOD(method) virtual CANVASNOTHROW Canvas::Result CANVASAPI method
#define CANVASMETHOD_(retType, method) virtual CANVASNOTHROW retType CANVASAPI method
#define CANVASMETHODIMP Canvas::Result
#define CANVASMETHODIMP_(retType) retType
#define CANVAS_INTERFACE struct
#define CANVAS_INTERFACE_DECLARE(iid) static const InterfaceId IId = InterfaceId::##iid;

#define CANVAS_PPV_ARGS(ppObj) \
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
template<class _Type>
class CCanvasPtr
{
    _Type *m_p = nullptr;

public:
    CCanvasPtr() = default;
    CCanvasPtr(_Type *p) :
        m_p(p)
    {
        if (m_p)
        {
            m_p->AddRef();
        }
    }
    CCanvasPtr(const CCanvasPtr &o) :
        m_p(o.m_p)
    {
        if (m_p)
        {
            m_p->AddRef();
        }
    }
    CCanvasPtr(CCanvasPtr &&o) :
        m_p(o.m_p)
    {
        o.m_p = nullptr;
    }

    ~CCanvasPtr()
    {
        if (m_p)
        {
            m_p->Release();
        }
    }

    CCanvasPtr &operator=(_Type *p)
    {
        if (m_p)
        {
            m_p->Release();
        }

        m_p = p;
        if (p)
        {
            m_p->AddRef();
        }

        return *this;
    }

    CCanvasPtr &operator=(const CCanvasPtr &o)
    {
        if (m_p)
        {
            m_p->Release();
        }

        m_p = o.m_p;
        m_p->AddRef();
        return *this;
    }

    CCanvasPtr &operator=(CCanvasPtr &&o)
    {
        if (m_p)
        {
            m_p->Release();
        }
        m_p = o.m_p;
        o.m_p = nullptr;
        return *this;
    }

    _Type **operator&()
    {
        return &m_p;
    }

    _Type *const*operator&() const
    {
        return &m_p;
    }

    void Detach()
    {
        m_p = nullptr;
    }

    _Type *Get() { return m_p; }
    const _Type *Get() const { return m_p; }

    operator _Type *() { return m_p; }
    operator const _Type *() const { return m_p; }

    _Type *operator->() { return m_p; }
    const _Type *operator->() const { return m_p; }
};

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
enum class InterfaceId : unsigned
{
    XUnknown = 0,
    XGeneric = 1,
    XCanvas,
    XScene,
    XSceneGraphNode,
    XSceneGraphIterator,
    XModelInstance,
    XCamera,
    XLight,
    XTransform,
    XTexture,
    XMaterial,
    XMesh,
    XAmination,
    XSkeleton,
    XIterator,
    XTreeIterator
};

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

    CANVASMETHOD(CreateScene)(InterfaceId iid, _Outptr_ void **ppScene) = 0;
    CANVASMETHOD(CreateTransformObject)(InterfaceId iid, _Outptr_ void **ppObj) = 0;
    CANVASMETHOD(CreateCameraObject)(InterfaceId iid, _Outptr_ void **ppObj) = 0;
    CANVASMETHOD(CreateLightObject)(InterfaceId iid, _Outptr_ void **ppObj) = 0;
    CANVASMETHOD(CreateModelInstanceObject)(InterfaceId iid, _Outptr_ void **ppObj) = 0;
    CANVASMETHOD(CreateCustomObject)(InterfaceId *pInnerInterfaces, UINT numInnerInterfaces, _Outptr_ void **ppObj) = 0;
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

}

extern Canvas::Result CANVASAPI CreateCanvas(Canvas::InterfaceId iid, _Outptr_ void **ppCanvas);

