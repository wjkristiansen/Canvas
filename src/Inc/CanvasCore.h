//================================================================================================
// CanvasCore
//================================================================================================

#pragma once

namespace Canvas
{
// Canvas core interfaces
GOM_INTERFACE XCanvas;

// Scene GOM_INTERFACE
GOM_INTERFACE XScene;

// Scene graph node
GOM_INTERFACE XSceneGraphNode;

// Camera GOM_INTERFACEs
GOM_INTERFACE XCamera;

// Light GOM_INTERFACEs
GOM_INTERFACE XLight;

// Transform GOM_INTERFACEs
GOM_INTERFACE XTransform;

// Assets
GOM_INTERFACE XTexture;
GOM_INTERFACE XMaterial;
GOM_INTERFACE XMesh;
GOM_INTERFACE XAmination;
GOM_INTERFACE XSkeleton;

//------------------------------------------------------------------------------------------------
template<class _Type>
class TCanvasPtr
{
    _Type *m_p = nullptr;

public:
    TCanvasPtr() = default;
    TCanvasPtr(_Type *p) :
        m_p(p)
    {
        if (m_p)
        {
            m_p->AddRef();
        }
    }
    TCanvasPtr(const TCanvasPtr &o) :
        m_p(o.m_p)
    {
        if (m_p)
        {
            m_p->AddRef();
        }
    }
    TCanvasPtr(TCanvasPtr &&o) :
        m_p(o.m_p)
    {
        o.m_p = nullptr;
    }

    ~TCanvasPtr()
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

    TCanvasPtr &operator=(_Type *p)
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

    TCanvasPtr &operator=(const TCanvasPtr &o)
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

    TCanvasPtr &operator=(TCanvasPtr &&o)
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
inline bool Failed(GOM::Result result)
{
    return result >= GOM::Result::Fail;
}

//------------------------------------------------------------------------------------------------
inline bool Succeeded(GOM::Result result)
{
    return result < GOM::Result::Fail;
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
GOM_INTERFACE XIterator : public GOM::XGeneric
{
    GOM_INTERFACE_DECLARE(CanvasIId_XIterator);

    // Resets the iterator to the start of the collection
    GOMMETHOD(Reset)() = 0;

    // Returns true if the the iterator is at the end of the collection
    GOMMETHOD_(bool, IsAtEnd)() = 0;

    // Moves the iterator to the next element
    GOMMETHOD(MoveNext)() = 0;

    // Moves the iterator to the previous element
    GOMMETHOD(MovePrev)() = 0;

    // QI's the current element (if exists)
    GOMMETHOD(Select)(GOM::InterfaceId iid, _Outptr_ void **ppObj) = 0;
    
    // Removes the current element and the iterator to the next element
    GOMMETHOD(Prune)() = 0;
};

//------------------------------------------------------------------------------------------------
GOM_INTERFACE
XObjectName : public GOM::XGeneric
{
    GOM_INTERFACE_DECLARE(CanvasIId_XObjectName);

    GOMMETHOD_(PCWSTR, GetName)() = 0;
};

//------------------------------------------------------------------------------------------------
GOM_INTERFACE
XCanvas : public GOM::XGeneric
{
    GOM_INTERFACE_DECLARE(CanvasIId_XCanvas);

    GOMMETHOD(CreateScene)(GOM::InterfaceId iid, _Outptr_ void **ppObj) = 0;
    GOMMETHOD(CreateObject)(ObjectType type, GOM::InterfaceId iid, _Outptr_ void **ppObj, PCWSTR szName = nullptr) = 0;
    GOMMETHOD(GetNamedObject)(_In_z_ PCWSTR szName, GOM::InterfaceId iid, _Outptr_ void **ppObj) = 0;
};

//------------------------------------------------------------------------------------------------
GOM_INTERFACE
XModelInstance : public GOM::XGeneric
{
    GOM_INTERFACE_DECLARE(CanvasIId_XModelInstance);
};

//------------------------------------------------------------------------------------------------
GOM_INTERFACE
XCamera : public GOM::XGeneric
{
    GOM_INTERFACE_DECLARE(CanvasIId_XCamera);
};

//------------------------------------------------------------------------------------------------
GOM_INTERFACE
XLight : public GOM::XGeneric
{
    GOM_INTERFACE_DECLARE(CanvasIId_XLight);
};

//------------------------------------------------------------------------------------------------
GOM_INTERFACE
XTransform : public GOM::XGeneric
{
    GOM_INTERFACE_DECLARE(CanvasIId_XTransform);
};

//------------------------------------------------------------------------------------------------
GOM_INTERFACE
XSceneGraphNode : public GOM::XGeneric
{
    GOM_INTERFACE_DECLARE(CanvasIId_XSceneGraphNode);
    GOMMETHOD(AddChild)(_In_ XSceneGraphNode *pChild) = 0;
    GOMMETHOD(CreateChildIterator)(_Outptr_ XIterator **ppIterator) = 0;
};

//------------------------------------------------------------------------------------------------
GOM_INTERFACE
XScene : public GOM::XGeneric
{
    GOM_INTERFACE_DECLARE(CanvasIId_XScene);
};

}

extern GOM::Result GOMAPI CreateCanvas(GOM::InterfaceId iid, _Outptr_ void **ppCanvas);

