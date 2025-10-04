//================================================================================================
// CanvasCore
//================================================================================================

#include "pch.h"

#include "Canvas.h"
#include "Camera.h"
#include "Light.h"
#include "Scene.h"
#include "Transform.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
#define INTERFACE_ID_STRING_CASE(iface, id) if (iface##::IId == id) return #iface;
#define GS_INTERFACE_ID_STRING_CASE(iface, unused) if (iface##::IId == id) return #iface;
inline const char *IIdToString(const Gem::InterfaceId &id)
{
    FOR_EACH_CANVAS_INTERFACE(INTERFACE_ID_STRING_CASE, id)
    FOR_EACH_CANVASGFX_INTERFACE(GS_INTERFACE_ID_STRING_CASE, id)

    return nullptr;
}

//------------------------------------------------------------------------------------------------
CCanvas::~CCanvas()
{
    m_pCanvasGfx = nullptr;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateScene(XScene **ppScene)
{
    CFunctionSentinel Sentinel(Logger(), "XCanvas::CreateScene");

    try
    {
        Gem::TGemPtr<CScene> pObj;
        Gem::ThrowGemError(Gem::TGeneric<CScene>::Create(&pObj, this));
        *ppScene = pObj.Detach();
    }
    catch(std::bad_alloc &)
    {
        Sentinel.SetResultCode(Gem::Result::OutOfMemory);
        *ppScene = nullptr;
        return Gem::Result::OutOfMemory;
    }
    catch(const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateSceneGraphNode(XSceneGraphNode **ppNode)
{
    CFunctionSentinel Sentinel(Logger(), "XCanvas::CreateSceneGraphNode");

    if (!ppNode)
    {
        return Gem::Result::BadPointer;
    }

    CSceneGraphNode *pNode = nullptr;
    auto result = Gem::TGeneric<CSceneGraphNode>::Create(&pNode, this);
    
    if (Gem::Succeeded(result))
    {
        *ppNode = pNode;
    }
    else
    {
        *ppNode = nullptr;
    }
    
    return result;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateCamera(XCamera **ppCamera)
{
    CFunctionSentinel Sentinel(Logger(), "XCanvas::CreateCamera");

    try
    {
        Gem::TGemPtr<CCamera> pCamera;
        Gem::ThrowGemError(Gem::TGeneric<CCamera>::Create(&pCamera, this));
        *ppCamera = pCamera.Detach();
    }
    catch (const Gem::GemError &e)
    {

    }
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateLight(XLight **ppLight)
{
    return Gem::Result::NotImplemented;
}

//------------------------------------------------------------------------------------------------
Gem::Result GEMAPI CreateCanvas(XCanvas **ppCanvas, std::shared_ptr<QLog::Logger> pLogger)
{
    *ppCanvas = nullptr;

    try
    {
        if (pLogger)
        {
            pLogger->Info("CANVAS: CreateCanvas: Creating canvas object...");
        }
        Gem::TGemPtr<CCanvas> pCanvas = new Gem::TGeneric<CCanvas>(pLogger); // throw(bad_alloc)
        *ppCanvas = pCanvas.Detach();
    }
    catch (std::bad_alloc &)
    {
        if (pLogger)
        {
            pLogger->Error("CANVAS: FAILURE in CreateCanvas");
        }
        return Gem::Result::OutOfMemory;
    }

    return Gem::Result::Success;
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
            ThrowGemError(Gem::Result::NotFound);
        }

        // Create XGfxInstance interface
        CreateCanvasGfxProc pCreate = reinterpret_cast<CreateCanvasGfxProc>(
            GetProcAddress(graphicsModule.get(), "CreateGfxInstance"));
        if (pCreate == nullptr)
        {
            throw(Gem::GemError(Gem::Result::NotFound));
        }

        Gem::TGemPtr<XGfxInstance> pCanvasGfx;
        ThrowGemError(pCreate(&pCanvasGfx, m_Logger));

        pCanvasGfx->AddRef();
        *ppCanvasGfx = pCanvasGfx.Get();

        m_pCanvasGfx.Attach(pCanvasGfx.Detach());

        graphicsModule.swap(m_GraphicsModule);
    }
    catch (const Gem::GemError &e)
    {
        Sentinel.SetResultCode(e.Result());
        return Gem::Result::Fail;
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
// Updates application logic and submits work to the graphics engine
GEMMETHODIMP CCanvas::FrameTick()
{
    Gem::Result result = Gem::Result::Success;
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

}