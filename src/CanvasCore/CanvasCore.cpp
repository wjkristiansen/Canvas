//================================================================================================
// CanvasCore
//================================================================================================

#include "stdafx.h"

using namespace Canvas;

    
//------------------------------------------------------------------------------------------------
static void DefaultOutputProc(LOG_OUTPUT_LEVEL Level, PCWSTR szString)
{
    // Write the string to the debugger followed by a newline
    OutputDebugStringW(szString);
    OutputDebugStringW(L"\n");

    // Write the string to the stdout
    std::wcout << szString << L"\n";
}

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
        WriteToLog(LOG_OUTPUT_LEVEL_ERROR, L"Out of memory XCanvas::CreateScene");
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
        WriteToLog(LOG_OUTPUT_LEVEL_ERROR, L"Out of memory XCanvas::CreateSceneGraphNode");
        return Result::OutOfMemory;
    }
}

//------------------------------------------------------------------------------------------------
void CCanvas::ReportObjectLeaks()
{
    for (auto pNode = m_OutstandingObjects.GetFirst(); pNode != m_OutstandingObjects.GetEnd(); pNode = pNode->GetNext())
    {
        std::wostringstream ostr;
        CCanvasObjectBase *pObject = pNode->Ptr();

        std::wcout << L"Leaked object: ";
        //std::wcout << L"Type=" << to_string(pObject->GetType()) << L", ";
        XObjectName *pObjectName;
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
        
        WriteToLog(LOG_OUTPUT_LEVEL_ERROR, ostr.str().c_str());
    }
}

//------------------------------------------------------------------------------------------------
Result GEMAPI CreateCanvas(InterfaceId iid, void **ppCanvas, CLog *pLog)
{
    *ppCanvas = nullptr;

    try
    {
        if (iid == XCanvas::IId)
        {
            TGemPtr<CCanvas> pCanvas = new TGeneric<CCanvas>(pLog); // throw(bad_alloc)
            pCanvas->WriteToLog(LOG_OUTPUT_LEVEL_MESSAGE, L"CreateCanvas");
            return pCanvas->QueryInterface(iid, ppCanvas);
        }
    }
    catch (std::bad_alloc &)
    {
        if (pLog)
        {
            pLog->WriteToLog(LOG_OUTPUT_LEVEL_ERROR, L"FAILURE in CreateCanvas");
        }
        return Result::OutOfMemory;
    }

    return Result::NoInterface;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateGraphicsDevice(CANVAS_GRAPHICS_OPTIONS *pGraphicsOptions, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice)
{
    Result result = Result::NotImplemented;

    WriteToLog(LOG_OUTPUT_LEVEL_MESSAGE, L"CCanvas::CreateGraphicsDevice");

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
    WriteToLog(LOG_OUTPUT_LEVEL_VERBOSE, L"Begin CCanvas::FrameTick");

    // Elapse time

    // Update scene graph

    // Build the display list

    // Render the display list

    m_pGraphicsDevice->RenderFrame();

    WriteToLog(LOG_OUTPUT_LEVEL_VERBOSE, L"End CCanvas::FrameTick");

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
        WriteToLog(LOG_OUTPUT_LEVEL_MESSAGE, L"CCanvas::SetupD3D12");
        ThrowGemError(CreateGraphicsDevice12(this, &pGraphicsDevice, hWnd));

        if (ppGraphicsDevice)
        {
            pGraphicsDevice->AddRef();
            *ppGraphicsDevice = pGraphicsDevice;
        }
    }
    catch (GemError &gomError)
    {
        WriteToLog(LOG_OUTPUT_LEVEL_ERROR, L"ERROR in CCanvas::SetupD3D12");
        return gomError.Result();
    }

    m_pGraphicsDevice = std::move(pGraphicsDevice);

    return Result::Success;
}

void CCanvas::WriteToLog(LOG_OUTPUT_LEVEL Level, PCWSTR szLogString)
{
    std::unique_lock<std::mutex> Lock(m_Mutex);
    static PCWSTR PrefixStrings[] =
    {
        L"CANVAS ERROR: ", // LOG_OUTPUT_LEVEL_ERROR
        L"CANVAS WARNING: ", // LOG_OUTPUT_LEVEL_WARNING
        L"CANVAS MESSAGE: ", // LOG_OUTPUT_LEVEL_MESSAGE
        L"CANVAS: ", // LOG_OUTPUT_LEVEL_VERBOSE
    };

    if (m_pLog && Level <= m_MaxLogOutputLevel)
    {
        std::wostringstream ostr;
        ostr << PrefixStrings[Level] << szLogString;
        m_pLog->WriteToLog(Level, ostr.str().c_str());
    }
}

