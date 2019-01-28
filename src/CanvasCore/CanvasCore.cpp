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
GEMMETHODIMP CCanvas::CreateSceneGraphNode(InterfaceId iid, _Outptr_ void **ppObj, PCWSTR szName)
{
    try
    {
        TGemPtr<XGeneric> pObj = new TGeneric<CSceneGraphNodeObject>(this, szName); // throw(std::bad_alloc)
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
        //std::wcout << L"Type=" << to_string(pObject->GetType()) << L", ";
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
GEMMETHODIMP CCanvas::CreateGraphicsDevice(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice)
{
    Result result = Result::NotImplemented;

    switch (pGraphicsOptions->Subsystem)
    {
    case GraphicsSubsystem::D3D12:
        result = SetupD3D12(pGraphicsOptions, hWnd, ppGraphicsDevice);
        break;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
// Updates application logic and submits work to the graphics engine
GEMMETHODIMP CCanvas::FrameTick()
{
    Result result = Result::Success;

    // Elapse time

    // Update scene graph

    // Build the display list

    // Render the display list

    m_pGraphicsDevice->RenderFrame();

    return result;
}

//------------------------------------------------------------------------------------------------
Result CCanvas::SetupD3D12(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice)
{
    if (ppGraphicsDevice)
    {
        *ppGraphicsDevice = nullptr;
    }

    TGemPtr<CGraphicsDevice> pGraphicsDevice;
    try
    {
        ThrowGemError(CreateGraphicsDevice12(&pGraphicsDevice, hWnd));

        if (ppGraphicsDevice)
        {
            pGraphicsDevice->AddRef();
            *ppGraphicsDevice = pGraphicsDevice;
        }
    }
    catch (GemError &gomError)
    {
        return gomError.Result();
    }

    m_pGraphicsDevice = std::move(pGraphicsDevice);

    return Result::Success;
}