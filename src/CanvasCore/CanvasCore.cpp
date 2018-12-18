//================================================================================================
// CanvasCore
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::InternalQueryInterface(InterfaceId iid, _Outptr_ void **ppObj)
{
    *ppObj = nullptr;
    switch (iid)
    {
    case XCanvas::IId:
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
class TCanvasObject<ObjectType::Null> :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    TInnerGeneric<CObjectName> m_ObjectName;
    TCanvasObject(CCanvas *pCanvas, PCWSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_ObjectName(this, szName, pCanvas)
    {
    }

    GEMMETHOD_(ObjectType, GetType)() const { return ObjectType::Null; }

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XObjectName::IId == iid)
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
GEMMETHODIMP CCanvas::CreateScene(InterfaceId iid, _Outptr_ void **ppObj)
{
    try
    {
        TGemPtr<XGeneric> pObj;
        pObj = new TGeneric<CScene>(this, L"SceneRoot"); // throw(std::bad_alloc)
        return pObj->QueryInterface(iid, ppObj);
    }
    catch(std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateObject(ObjectType type, InterfaceId iid, _Outptr_ void **ppObj, PCWSTR szName)
{
    try
    {
        TGemPtr<XGeneric> pObj;
        switch (type)
        {
        case ObjectType::Null:
            pObj = new TGeneric<TCanvasObject<ObjectType::Null>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::SceneGraphNode:
            pObj = new TGeneric<TCanvasObject<ObjectType::SceneGraphNode>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::Transform:
            pObj = new TGeneric<TCanvasObject<ObjectType::Transform>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::Light:
            pObj = new TGeneric<TCanvasObject<ObjectType::Light>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::Camera:
            pObj = new TGeneric<TCanvasObject<ObjectType::Camera>>(this, szName); // throw(std::bad_alloc)
            break;

        case ObjectType::ModelInstance:
            pObj = new TGeneric<TCanvasObject<ObjectType::ModelInstance>>(this, szName); // throw(std::bad_alloc)
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
        std::wcout << L"Leaked object: ";
        std::wcout << L"Type=" << to_string(pObject->GetType()) << L", ";
        XObjectName *pObjectName;
        if (Succeeded(pObject->InternalQueryInterface(GEM_IID_PPV_ARGS(&pObjectName))))
        {
            pObjectName->Release();
            std::wcout << L"Name=\"" << pObjectName->GetName() << L"\", ";
        }
        XGeneric *pXGeneric;
        if (Succeeded(pObject->InternalQueryInterface(GEM_IID_PPV_ARGS(&pXGeneric))))
        {
            ULONG RefCount = pXGeneric->Release();
            std::wcout << L"RefCount=" << RefCount;
        }
        std::wcout << std::endl;
    }
}

//------------------------------------------------------------------------------------------------
Result GEMAPI CreateCanvas(InterfaceId iid, void **ppCanvas)
{
    *ppCanvas = nullptr;

    try
    {
        if (iid == XCanvas::IId)
        {
            TGemPtr<CCanvas> pCanvas = new TGeneric<CCanvas>; // throw(bad_alloc)
            return pCanvas->QueryInterface(iid, ppCanvas);
        }
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GOMMETHODIMP CCanvas::SetupGraphics(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions)
{
    switch (pGraphicsOptions->Subsystem)
    {
    case GraphicsSubsystem::D3D12:
        return SetupD3D12(pGraphicsOptions);
        break;

    default:
        return Result::NotImplemented;
        break;
    }
}

//------------------------------------------------------------------------------------------------
Result CCanvas::SetupD3D12(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions)
{
    TGomPtr<TGeneric<CGraphicsDevice>> pGraphicsDevice;
    try
    {
        ThrowGomError(CreateGraphicsDevice12(&pGraphicsDevice));
    }
    catch (GomError &gomError)
    {
        return gomError.Result();
    }
}