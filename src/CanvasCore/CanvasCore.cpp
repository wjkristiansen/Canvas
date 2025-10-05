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
// Global logger instance
std::unique_ptr<QLog::Logger> g_pLogger;

//------------------------------------------------------------------------------------------------
CANVAS_API Gem::Result InitCanvasLogger(QLog::Sink &sink, QLog::Level level)
{
    if (g_pLogger)
        return Gem::Result::Fail;

    g_pLogger = std::make_unique<QLog::Logger>(sink, level);

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
CANVAS_API QLog::Logger *GetCanvasLogger()
{
    return g_pLogger.get();
}

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
    // Shutdown the render queue manager before releasing graphics
    m_RenderQueueManager.Shutdown();
    m_pCanvasGfx = nullptr;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateScene(XScene **ppScene)
{
    CFunctionSentinel Sentinel("XCanvas::CreateScene");

    try
    {
        Gem::TGemPtr<CScene> pObj;
        Gem::ThrowGemError(Gem::TGenericImpl<CScene>::Create(&pObj, this));
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
    CFunctionSentinel Sentinel("XCanvas::CreateSceneGraphNode");

    if (!ppNode)
    {
        return Gem::Result::BadPointer;
    }

    CSceneGraphNode *pNode = nullptr;
    auto result = Gem::TGenericImpl<CSceneGraphNode>::Create(&pNode, this);
    
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
    CFunctionSentinel Sentinel("XCanvas::CreateCamera");

    try
    {
        Gem::TGemPtr<CCamera> pCamera;
        Gem::ThrowGemError(Gem::TGenericImpl<CCamera>::Create(&pCamera, this));
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
Gem::Result CANVAS_API CreateCanvas(XCanvas **ppCanvas)
{
    *ppCanvas = nullptr;

    auto pLogger = GetCanvasLogger();

    try
    {
        if (pLogger)
        {
            pLogger->Info("CANVAS: CreateCanvas: Creating canvas object...");
        }
        Gem::TGemPtr<CCanvas> pCanvas;
        Gem::ThrowGemError(Gem::TGenericImpl<CCanvas>::Create(&pCanvas)); // throw(Gem::GemError)
        *ppCanvas = pCanvas.Detach();

        return Gem::Result::Success;
    }
    catch (const Gem::GemError &e)
    {
        if (pLogger)
        {
            pLogger->Error("CANVAS: FAILURE in CreateCanvas");
        }
        return e.Result();
    }
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::InitCanvasGfx(PCSTR szDLLPath, _Outptr_result_nullonfailure_ XGfxInstance **ppCanvasGfx)
{
    CFunctionSentinel Sentinel("XCanvas::InitCanvasGfx");

    try
    {
        *ppCanvasGfx = nullptr;

        wil::unique_hmodule graphicsModule(LoadLibraryExA(szDLLPath, NULL, 0));

        if (graphicsModule.get() == NULL)
        {
            Gem::ThrowGemError(Gem::Result::NotFound);
        }

        // Create XGfxInstance interface
        CreateCanvasGfxProc pCreate = reinterpret_cast<CreateCanvasGfxProc>(
            GetProcAddress(graphicsModule.get(), "CreateGfxInstance"));
        if (pCreate == nullptr)
        {
            throw(Gem::GemError(Gem::Result::NotFound));
        }

        Gem::TGemPtr<XGfxInstance> pCanvasGfx;
        Gem::ThrowGemError(pCreate(&pCanvasGfx));

        pCanvasGfx->AddRef();
        *ppCanvasGfx = pCanvasGfx.Get();

        m_pCanvasGfx.Attach(pCanvasGfx.Detach());

        // Initialize the render queue manager with the graphics device
        Gem::TGemPtr<XGfxDevice> pDevice;
        Gem::Result result = m_pCanvasGfx->CreateGfxDevice(&pDevice);
        if (result == Gem::Result::Success)
        {
            m_RenderQueueManager.Initialize(pDevice.Get());
        }

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