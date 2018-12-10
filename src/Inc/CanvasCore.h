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

    void Detach()
    {
        m_p = nullptr;
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
        auto temp = m_p;

        m_p = o.m_p;

        if (temp != m_p)
        {
            if (m_p)
            {
                m_p->AddRef();
            }

            if (temp)
            {
                temp->Release();
            }
        }

        return *this;
    }

    CCanvasPtr &operator=(CCanvasPtr &&o)
    {
        if (m_p != o.m_p)
        {
            auto temp = m_p;

            m_p = o.m_p;

            if (temp)
            {
                temp->Release();
            }
        }
        o.m_p = nullptr;

        return *this;
    }

    _Type **operator&()
    {
        return &m_p;
    }

    _Type &operator*() const
    {
        return *m_p;
    }

    _Type *Get() const { return m_p; }

    operator _Type *() const { return m_p; }
    
    _Type *operator->() const { return m_p; }
};

//------------------------------------------------------------------------------------------------
enum class Result : UINT32
{
    Success = 0,
    End = 1,
    Fail = 0x80000000, // Must be first failure
    InvalidArg,
    NotFound,
    OutOfMemory,
    NoInterface,
    BadPointer,
    NotImplemented,
    DuplicateKey,
    Uninitialized,
};

//------------------------------------------------------------------------------------------------
inline bool Failed(Result result)
{
    return result >= Result::Fail;
}

//------------------------------------------------------------------------------------------------
inline bool Succeeded(Result result)
{
    return result < Result::Fail;
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
    XObjectName,
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

    // Moves the iterator to the next element
    CANVASMETHOD(MoveNext)() = 0;

    // Moves the iterator to the previous element
    CANVASMETHOD(MovePrev)() = 0;

    // QI's the current element (if exists)
    CANVASMETHOD(Select)(InterfaceId iid, _Outptr_ void **ppObj) = 0;
    
    // Removes the current element and the iterator to the next element
    CANVASMETHOD(Prune)() = 0;
};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE
XObjectName : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XObjectName)
    CANVASMETHOD_(PCSTR, GetName)() = 0;
};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE
XCanvas : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XCanvas)

    CANVASMETHOD(CreateScene)(InterfaceId iid, _Outptr_ void **ppScene) = 0;

    CANVASMETHOD(CreateTransformObject)(_In_z_ InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName = nullptr) = 0;
    CANVASMETHOD(CreateCameraObject)(_In_z_ InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName = nullptr) = 0;
    CANVASMETHOD(CreateLightObject)(_In_z_ InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName = nullptr) = 0;
    CANVASMETHOD(CreateModelInstanceObject)(_In_z_ InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName = nullptr) = 0;
    CANVASMETHOD(CreateCustomObject)(_In_z_ InterfaceId *pInnerInterfaces, UINT numInnerInterfaces, _Outptr_ void **ppObj, PCSTR szName = nullptr) = 0;

    CANVASMETHOD(GetNamedObject)(_In_z_ PCSTR szName, InterfaceId iid, _Outptr_ void **ppObj) = 0;
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
XSceneGraphNode : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XSceneGraphNode)

    CANVASMETHOD(AddChild)(_In_ XSceneGraphNode *pChild) = 0;
    CANVASMETHOD(CreateChildIterator)(_Outptr_ XIterator **ppIterator) = 0;
};

//------------------------------------------------------------------------------------------------
CANVAS_INTERFACE
XScene : public XGeneric
{
    CANVAS_INTERFACE_DECLARE(XScene)
};

}

extern Canvas::Result CANVASAPI CreateCanvas(Canvas::InterfaceId iid, _Outptr_ void **ppCanvas);

