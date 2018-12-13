//================================================================================================
// CanvasCore
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CCanvas::InternalQueryInterface(InterfaceId iid, _Outptr_ void **ppObj)
{
    *ppObj = nullptr;
    switch (iid)
    {
    case InterfaceId::XCanvas:
        *ppObj = this;
        AddRef();
        break;

    default:
        return __super::InternalQueryInterface(iid, ppObj);
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
template <>
class CCanvasObject<ObjectType::Null> :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    CInnerGeneric<CObjectName> m_ObjectName;
    CCanvasObject(CCanvas *pCanvas, PCSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_ObjectName(this, szName, pCanvas)
    {
    }

    CANVASMETHOD_(ObjectType, GetType)() const { return ObjectType::Null; }

    CANVASMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (InterfaceId::XObjectName == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

//------------------------------------------------------------------------------------------------
CCanvas::~CCanvas()
{
    ReportObjectLeaks();
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CCanvas::CreateScene(InterfaceId iid, _Outptr_ void **ppObj)
{
    try
    {
        CCanvasPtr<XGeneric> pObj;
        pObj = new CGeneric<CScene>(this, "SceneRoot"); // throw(std::bad_alloc)
        return pObj->QueryInterface(iid, ppObj);
    }
    catch(std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
}

//------------------------------------------------------------------------------------------------
CANVASMETHODIMP CCanvas::CreateObject(ObjectType type, InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName)
{
    try
    {
        CCanvasPtr<XGeneric> pObj;
        switch (type)
        {
        case ObjectType::Null:
            pObj = new CGeneric<CCanvasObject<ObjectType::Null>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::SceneGraphNode:
            pObj = new CGeneric<CCanvasObject<ObjectType::SceneGraphNode>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::Transform:
            pObj = new CGeneric<CCanvasObject<ObjectType::Transform>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::Light:
            pObj = new CGeneric<CCanvasObject<ObjectType::Light>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::Camera:
            pObj = new CGeneric<CCanvasObject<ObjectType::Camera>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::ModelInstance:
            pObj = new CGeneric<CCanvasObject<ObjectType::ModelInstance>>(this, szName); // throw(std::bad_alloc)
            break;
        default:
            return Result::NoInterface;
        }
        return pObj->QueryInterface(iid, ppObj);
    }
    catch(std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
}

//------------------------------------------------------------------------------------------------
void CCanvas::ReportObjectLeaks()
{
    for (CCanvasObjectBase *pObject : m_OutstandingObjects)
    {
        std::cout << "Leaked object: ";
        std::cout << "Type=" << to_string(pObject->GetType()) << ", ";
        XObjectName *pObjectName;
        if (Succeeded(pObject->InternalQueryInterface(CANVAS_PPV_ARGS(&pObjectName))))
        {
            pObjectName->Release();
            std::cout << "Name=\"" << pObjectName->GetName() << "\", ";
        }
        XGeneric *pXGeneric;
        if (Succeeded(pObject->InternalQueryInterface(CANVAS_PPV_ARGS(&pXGeneric))))
        {
            ULONG RefCount = pXGeneric->Release();
            std::cout << "RefCount=" << RefCount;
        }
        std::cout << std::endl;
    }
}

//------------------------------------------------------------------------------------------------
Result CANVASAPI CreateCanvas(InterfaceId iid, void **ppCanvas)
{
    *ppCanvas = nullptr;

    try
    {
        if (iid == XCanvas::IId)
        {
            CCanvasPtr<CCanvas> pCanvas = new CGeneric<CCanvas>; // throw(bad_alloc)
            return pCanvas->QueryInterface(iid, ppCanvas);
        }
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }

    return Result::Success;
}
