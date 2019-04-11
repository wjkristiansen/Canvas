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
        *ppObj = reinterpret_cast<XCanvas *>(this);
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
    m_pGraphicsDevice = nullptr;

    ReportObjectLeaks();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateScene(InterfaceId iid, _Outptr_ void **ppObj)
{
    try
    {
        TGemPtr<XGeneric> pObj;
        pObj = new TGeneric<CScene>(this, L"Scene"); // throw(std::bad_alloc)
        return pObj->QueryInterface(iid, ppObj);
    }
    catch(std::bad_alloc &)
    {
        Logger().LogError(L"Out of memory XCanvas::CreateScene");
        return Result::OutOfMemory;
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateSceneGraphNode(InterfaceId iid, _Outptr_ void **ppObj, PCWSTR szName)
{
    try
    {
        TGemPtr<XGeneric> pObj = new TGeneric<CSceneGraphNode>(this, szName); // throw(std::bad_alloc)
        return pObj->QueryInterface(iid, ppObj);
    }
    catch(std::bad_alloc &)
    {
        Logger().LogError(L"Out of memory XCanvas::CreateSceneGraphNode");
        return Result::OutOfMemory;
    }
}

//------------------------------------------------------------------------------------------------
void CCanvas::ReportObjectLeaks()
{
    for (auto pNode = m_OutstandingObjects.GetFirst(); pNode != m_OutstandingObjects.GetEnd(); pNode = pNode->GetNext())
    {
        std::wostringstream ostr;
        CObjectBase *pObject = pNode->Ptr();

        std::wcout << L"Leaked object: ";
        //std::wcout << L"Type=" << to_string(pObject->GetType()) << L", ";
        XName *pObjectName;
        if (Succeeded(pObject->InternalQueryInterface(GEM_IID_PPV_ARGS(&pObjectName))))
        {
            pObjectName->Release();
            ostr << L"Name=\"" << pObjectName->GetName() << L"\", ";
        }
        XGeneric *pXGeneric;
        if (Succeeded(pObject->InternalQueryInterface(GEM_IID_PPV_ARGS(&pXGeneric))))
        {
            ULONG RefCount = pXGeneric->Release();
            ostr << L"RefCount=" << RefCount;
        }
        
        Logger().LogError(ostr.str().c_str());
    }
}

//------------------------------------------------------------------------------------------------
Result GEMAPI CreateCanvas(InterfaceId iid, void **ppCanvas, SlimLog::CLogOutputBase *pLogOutput)
{
    *ppCanvas = nullptr;

    try
    {
        if (iid == XCanvas::IId)
        {
            if (pLogOutput)
            {
                pLogOutput->Output(L"CANVAS", L"CreateCanvas: Creating canvas object...");
            }
            TGemPtr<CCanvas> pCanvas = new TGeneric<CCanvas>(pLogOutput); // throw(bad_alloc)
            return pCanvas->QueryInterface(iid, ppCanvas);
        }
    }
    catch (std::bad_alloc &)
    {
        if (pLogOutput)
        {
            pLogOutput->Output(L"CANVAS ERROR: ", L"FAILURE in CreateCanvas");
        }
        return Result::OutOfMemory;
    }

    return Result::NoInterface;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateGraphicsDevice(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice)
{
    Result result = Result::NotImplemented;

    Logger().LogMessage(L"CCanvas::CreateGraphicsDevice");

    switch (pGraphicsOptions->Subsystem)
    {
    case GraphicsSubsystem::D3D12:
        result = SetupD3D12(pGraphicsOptions, hWnd, ppGraphicsDevice);
        break;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateGraphicsDevice(PCWSTR szDLLPath, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice)
{
    Logger().LogMessage(L"CCanvas::CreateGraphicsDevice");
    Result result = Result::NotImplemented;

    try
    {
        CModule Module(LoadLibraryExW(szDLLPath, NULL, 0));

        if (Module.Get() == NULL)
        {
            throw(std::exception("LoadLibrary"));
        }

        CreateCanvasGraphicsDevice pCreate = reinterpret_cast<CreateCanvasGraphicsDevice>(
            GetProcAddress(Module.Get(), "CreateCanvasGraphicsDevice"));
        if (pCreate == nullptr)
        {
            throw(std::exception("GetProcAddress"));
        }

        Gem::TGemPtr<XGraphicsDevice> pGraphicsDevice;
        ThrowGemError(pCreate(this, &pGraphicsDevice));

        result = Result::Success;
    }
    catch(const std::exception &e)
    {
        Logger().LogErrorF(L"XCanvas::CreateGraphicsDevice failed: %S", e.what());
        result = Result::NotFound;
    }
    catch (const Gem::GemError &e)
    {
        Logger().LogErrorF(L"XCanvas::CreateGraphicsDevice failed: %s", Gem::ResultToString(e.Result()));
        result = Result::NotFound;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
// Updates application logic and submits work to the graphics engine
GEMMETHODIMP CCanvas::FrameTick()
{
    Result result = Result::Success;
    Logger().LogInfo(L"Begin CCanvas::FrameTick");

    // Elapse time

    // Update scene graph

    // Build the display list

    // Render the display list

    m_pGraphicsDevice->RenderFrame();

    ++m_FrameCounter;
    if (m_FrameCounter == 1200)
    {
        UINT64 FrameEndTime = m_FrameTimer.Now();

        if (m_FrameEndTimeLast > 0)
        {
            UINT64 DTime = FrameEndTime - m_FrameEndTimeLast;
            UINT64 FramesPerSecond = m_FrameCounter * 1000 / CTimer::Milliseconds(DTime);
            std::wcout << L"FPS: " << FramesPerSecond << std::endl;
        }
        m_FrameEndTimeLast = FrameEndTime;
        m_FrameCounter = 0;
    }

    Logger().LogInfo(L"End CCanvas::FrameTick");

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
        Logger().LogMessage(L"CCanvas::SetupD3D12");
        ThrowGemError(CreateGraphicsDevice12(this, &pGraphicsDevice, hWnd));

        if (ppGraphicsDevice)
        {
            pGraphicsDevice->AddRef();
            *ppGraphicsDevice = pGraphicsDevice;
        }
    }
    catch (GemError &gomError)
    {
        Logger().LogError(L"ERROR in CCanvas::SetupD3D12");
        return gomError.Result();
    }

    m_pGraphicsDevice = std::move(pGraphicsDevice);

    return Result::Success;
}


