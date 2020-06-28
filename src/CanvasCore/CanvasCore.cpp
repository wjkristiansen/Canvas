//================================================================================================
// CanvasCore
//================================================================================================

#include "stdafx.h"

using namespace Canvas;


//------------------------------------------------------------------------------------------------
#define INTERFACE_ID_STRING_CASE(iface, unused) case CanvasIId_##iface: return #iface;
#define GS_INTERFACE_ID_STRING_CASE(iface, unused) case CanvasGfxIId_##iface: return #iface;
inline const char *IIdToString(Gem::InterfaceId id)
{
    switch (id.Value)
    {
        FOR_EACH_CANVAS_INTERFACE(INTERFACE_ID_STRING_CASE);
        FOR_EACH_CANVAS_GS_INTERFACE(GS_INTERFACE_ID_STRING_CASE);
    }

    return nullptr;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::InternalQueryInterface(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj)
{
    switch (iid.Value)
    {
	case XCanvas::IId:
        *ppObj = reinterpret_cast<XCanvas *>(this);
        AddRef();
        return Result::Success;
    }

    return CGenericBase::InternalQueryInterface(iid, ppObj);
}

//------------------------------------------------------------------------------------------------
CCanvas::~CCanvas()
{
    m_pGraphicsDevice = nullptr;

    ReportObjectLeaks();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateScene(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj)
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
GEMMETHODIMP CCanvas::CreateNullSceneGraphNode(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj, PCSTR szName)
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
GEMMETHODIMP CCanvas::CreateCameraNode(_In_ const ModelData::CAMERA_DATA *pCameraData, _Outptr_result_nullonfailure_ XCamera **ppCamera, _In_z_ PCSTR szName)
{
    try
    {
        TGemPtr<XCamera> pNode;
        pNode = new TGeneric<CCamera>(this, pCameraData, szName);
        *ppCamera = pNode.Detach();
    }
    catch (std::bad_alloc &)
    {
        *ppCamera = nullptr;
        return Result::OutOfMemory;
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateLightNode(const ModelData::LIGHT_DATA *pLightData, _Outptr_result_nullonfailure_ XLight **ppLight, _In_z_ PCSTR szName)
{
    try
    {
        TGemPtr<XLight> pNode;
        pNode = new TGeneric<CLight>(this, szName);
        *ppLight = pNode.Detach();
    }
    catch (std::bad_alloc &)
    {
        *ppLight = nullptr;
        return Result::OutOfMemory;
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CCanvas::ReportObjectLeaks()
{
    for (auto pNode = m_OutstandingObjects.GetFirst(); pNode != m_OutstandingObjects.GetEnd(); pNode = pNode->GetNext())
    {
        std::ostringstream ostr;
        CObjectBase *pObject = pNode->Ptr();

        ostr << "Leaked object: ";
		ostr << "Type=" << IIdToString(Gem::InterfaceId(pObject->GetMostDerivedType())) << ", ";
        XNameTag *pNameTag;
        if (Succeeded(pObject->InternalQueryInterface(GEM_IID_PPV_ARGS(&pNameTag))))
        {
            pNameTag->Release();
            auto *pName = pNameTag->GetName();
			if (pName)
			{
				ostr << "Name=\"" << pName << "\", ";
			}
			else
			{
				ostr << "Name=<Unnamed>, ";
			}
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
Result GEMAPI CreateCanvas(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppCanvas, QLog::CLogClient *pLogClient)
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
GEMMETHODIMP CCanvas::CreateGfxDevice(PCSTR szDLLPath, HWND hWnd, _Outptr_opt_result_nullonfailure_ Canvas::XCanvasGfxDevice **ppGraphicsDevice)
{
    Logger().LogInfo("CCanvas::CreateGfxDevice");
    Result result = Result::NotImplemented;

    if (ppGraphicsDevice)
    {
        *ppGraphicsDevice = nullptr;
    }

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

        Gem::TGemPtr<XCanvasGfxDevice> pGraphicsDevice;
        ThrowGemError(pCreate(&pGraphicsDevice, hWnd, m_Logger.GetLogClient()));
        Gem::TGemPtr<XCanvasGfxDevice> pXCanvasGfxDevice = pGraphicsDevice.Get();

        if (ppGraphicsDevice)
        {
            *ppGraphicsDevice = pXCanvasGfxDevice.Get();
        }
        m_pGraphicsDevice = pGraphicsDevice;
        pGraphicsDevice.Detach();

        result = Result::Success;
        m_GraphicsModule = std::move(Module);
    }
    catch (const std::exception &e)
    {
        Logger().LogErrorF("XCanvas::CreateGfxDevice failed: %s", e.what());
        result = Result::NotFound;
    }
    catch (const Gem::GemError &e)
    {
        Logger().LogErrorF("XCanvas::CreateGfxDevice failed: %s", Gem::ResultToString(e.Result()));
        result = Result::NotFound;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
// Updates application logic and submits work to the graphics engine
GEMMETHODIMP CCanvas::FrameTick()
{
    Result result = Result::Success;
//    Logger().LogInfo("Begin CCanvas::FrameTick");

    // Elapse time

    // Update scene graph

    // Build the display list

    // Render the display list

    ++m_FrameCounter;
    if (m_FrameCounter == 1200)
    {
        UINT64 FrameEndTime = m_FrameTimer.Now();

        if (m_FrameEndTimeLast > 0)
        {
            UINT64 DTime = CTimer::Microseconds(FrameEndTime - m_FrameEndTimeLast);
            UINT64 FramesPerSecond = DTime > 0 ? (m_FrameCounter * 1000000ULL) / DTime : UINT64_MAX;
            std::cout << "FPS: " << FramesPerSecond << std::endl;
        }
        m_FrameEndTimeLast = FrameEndTime;
        m_FrameCounter = 0;
    }

//    Logger().LogInfo("End CCanvas::FrameTick");

    return result;
}


