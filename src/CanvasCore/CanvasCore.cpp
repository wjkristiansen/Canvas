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
CCanvas::~CCanvas()
{
    m_pCanvasGfx = nullptr;

    ReportObjectLeaks();
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateScene(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj)
{
    CFunctionSentinel Sentinel(Logger(), "XCanvas::CreateScene");

    try
    {
        TGemPtr<XGeneric> pObj;
        pObj = new TGeneric<CScene>(this, "Scene"); // throw(std::bad_alloc)
        *ppObj = pObj.Detach();
    }
    catch(std::bad_alloc &)
    {
        Sentinel.SetResultCode(Result::OutOfMemory);
        *ppObj = nullptr;
        return Result::OutOfMemory;
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateNullSceneGraphNode(InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj, PCSTR szName)
{
    CFunctionSentinel Sentinel(Logger(), "XCanvas::CreateNullSceneGraphNode");

    try
    {
        TGemPtr<XSceneGraphNode> pNode;
        pNode = new TGeneric<CNullSceneGraphNode>(this, szName);
        *ppObj = pNode.Detach();
    }
    catch (std::bad_alloc &)
    {
        Sentinel.SetResultCode(Result::OutOfMemory);
        *ppObj = nullptr;
        return Result::OutOfMemory;
    }

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateCameraNode(_In_ const ModelData::CAMERA_DATA *pCameraData, _Outptr_result_nullonfailure_ XCamera **ppCamera, _In_z_ PCSTR szName)
{
    CFunctionSentinel Sentinel(Logger(), "XCanvas::CreateCameraNode");

    try
    {
        TGemPtr<XCamera> pNode;
        pNode = new TGeneric<CCamera>(this, pCameraData, szName);
        *ppCamera = pNode.Detach();
    }
    catch (std::bad_alloc &)
    {
        Sentinel.SetResultCode(Result::OutOfMemory);
        *ppCamera = nullptr;
        return Result::OutOfMemory;
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateLightNode(const ModelData::LIGHT_DATA *pLightData, _Outptr_result_nullonfailure_ XLight **ppLight, _In_z_ PCSTR szName)
{
    CFunctionSentinel Sentinel(Logger(), "XCanvas::CreateLightNode");

    try
    {
        TGemPtr<XLight> pNode;
        pNode = new TGeneric<CLight>(this, szName);
        *ppLight = pNode.Detach();
    }
    catch (std::bad_alloc &)
    {
        Sentinel.SetResultCode(Result::OutOfMemory);
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
            *ppCanvas = pCanvas.Detach();
        }
        else
        {
            return Result::NoInterface;
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

    return Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::InitCanvasGfx(PCSTR szDLLPath, _Outptr_result_nullonfailure_ XGfxInstance **ppCanvasGfx)
{
    CFunctionSentinel Sentinel(Logger(), "XCanvas::InitCanvasGfx");

    try
    {
        *ppCanvasGfx = nullptr;

        wil::unique_hmodule graphicsModule(LoadLibraryExA(szDLLPath, NULL, 0));

        if (graphicsModule.get() == NULL)
        {
            ThrowGemError(Result::NotFound);
        }

        // Create XGfxInstance interface
        CreateCanvasGfxProc pCreate = reinterpret_cast<CreateCanvasGfxProc>(
            GetProcAddress(graphicsModule.get(), "CreateGfxInstance"));
        if (pCreate == nullptr)
        {
            throw(GemError(Result::NotFound));
        }

        Gem::TGemPtr<XGfxInstance> pCanvasGfx;
        ThrowGemError(pCreate(&pCanvasGfx, m_Logger.GetLogClient()));

        pCanvasGfx->AddRef();
        *ppCanvasGfx = pCanvasGfx.Get();

        m_pCanvasGfx.Attach(pCanvasGfx.Detach());

        graphicsModule.swap(m_GraphicsModule);
    }
    catch (const Gem::GemError &e)
    {
        Sentinel.SetResultCode(e.Result());
        return Result::Fail;
    }

    return Result::Success;
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


