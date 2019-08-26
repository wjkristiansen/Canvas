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
        return CGenericBase::InternalQueryInterface(iid, ppObj);
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
        pObj = new TGeneric<CScene>(this, "Scene"); // throw(std::bad_alloc)
        return pObj->QueryInterface(iid, ppObj);
    }
    catch(std::bad_alloc &)
    {
        Logger().LogError("Out of memory XCanvas::CreateScene");
        *ppObj = nullptr;
        return Result::OutOfMemory;
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateNullSceneGraphNode(InterfaceId iid, _Outptr_ void **ppObj, PCSTR szName)
{
    try
    {
        TGemPtr<XSceneGraphNode> pNode;
        pNode = new TGeneric<TSceneGraphNode<XSceneGraphNode>>(this, szName);
        return pNode->QueryInterface(iid, ppObj);
    }
    catch (std::bad_alloc &)
    {
        *ppObj = nullptr;
        return Result::OutOfMemory;
    }
}

//------------------------------------------------------------------------------------------------
void CCanvas::ReportObjectLeaks()
{
    for (auto pNode = m_OutstandingObjects.GetFirst(); pNode != m_OutstandingObjects.GetEnd(); pNode = pNode->GetNext())
    {
        std::ostringstream ostr;
        CObjectBase *pObject = pNode->Ptr();

        ostr << "Leaked object: ";
        //std::cout << "Type=" << to_string(pObject->GetType()) << ", ";
        XNameTag *pNameTag;
        if (Succeeded(pObject->InternalQueryInterface(GEM_IID_PPV_ARGS(&pNameTag))))
        {
            pNameTag->Release();
            ostr << "Name=\"" << pNameTag->GetName() << "\", ";
        }
        XGeneric *pXGeneric;
        if (Succeeded(pObject->InternalQueryInterface(GEM_IID_PPV_ARGS(&pXGeneric))))
        {
            ULONG RefCount = pXGeneric->Release();
            ostr << "RefCount=" << RefCount;
        }
        
        Logger().LogError(ostr.str().c_str());
    }
}

//------------------------------------------------------------------------------------------------
Result GEMAPI CreateCanvas(InterfaceId iid, void **ppCanvas, QLog::CLogClient *pLogClient)
{
    *ppCanvas = nullptr;

    try
    {
        if (iid == XCanvas::IId)
        {
            if (pLogClient)
            {
                if (pLogClient->LogEntryBegin(QLog::Category::Info, "CANVAS", "CreateCanvas: Creating canvas object..."))
                {
                    pLogClient->LogEntryEnd();
                }
            }
            TGemPtr<CCanvas> pCanvas = new TGeneric<CCanvas>(pLogClient); // throw(bad_alloc)
            return pCanvas->QueryInterface(iid, ppCanvas);
        }
    }
    catch (std::bad_alloc &)
    {
        if (pLogClient)
        {
            if (pLogClient->LogEntryBegin(QLog::Category::Error, "CANVAS: ", "FAILURE in CreateCanvas"))
            {
                pLogClient->LogEntryEnd();
            }
        }
        return Result::OutOfMemory;
    }

    return Result::NoInterface;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateGraphicsDevice(PCSTR szDLLPath, HWND hWnd, _Outptr_opt_ XGraphicsDevice **ppGraphicsDevice)
{
    Logger().LogInfo("CCanvas::CreateGraphicsDevice");
    Result result = Result::NotImplemented;

    try
    {
        CModule Module(LoadLibraryExA(szDLLPath, NULL, 0));

        if (Module.Get() == NULL)
        {
            throw(std::exception("LoadLibrary"));
        }

        CreateCanvasGraphicsDeviceProc pCreate = reinterpret_cast<CreateCanvasGraphicsDeviceProc>(
            GetProcAddress(Module.Get(), "CreateCanvasGraphicsDevice"));
        if (pCreate == nullptr)
        {
            throw(std::exception("GetProcAddress"));
        }

        Gem::TGemPtr<CGraphicsDevice> pGraphicsDevice;
        ThrowGemError(pCreate(this, &pGraphicsDevice, hWnd));
        Gem::TGemPtr<XGraphicsDevice> pXGraphicsDevice = pGraphicsDevice.Get();

        if (ppGraphicsDevice)
        {
            *ppGraphicsDevice = pXGraphicsDevice.Get();
        }
        m_pGraphicsDevice = pGraphicsDevice;
        pGraphicsDevice.Detach();

        result = Result::Success;
        m_GraphicsModule = std::move(Module);
    }
    catch(const std::exception &e)
    {
        Logger().LogErrorF("XCanvas::CreateGraphicsDevice failed: %S", e.what());
        result = Result::NotFound;
    }
    catch (const Gem::GemError &e)
    {
        Logger().LogErrorF("XCanvas::CreateGraphicsDevice failed: %s", Gem::ResultToString(e.Result()));
        result = Result::NotFound;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
// Updates application logic and submits work to the graphics engine
GEMMETHODIMP CCanvas::FrameTick()
{
    Result result = Result::Success;
    Logger().LogInfo("Begin CCanvas::FrameTick");

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
            std::cout << "FPS: " << FramesPerSecond << std::endl;
        }
        m_FrameEndTimeLast = FrameEndTime;
        m_FrameCounter = 0;
    }

    Logger().LogInfo("End CCanvas::FrameTick");

    return result;
}


