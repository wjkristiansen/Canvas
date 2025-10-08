//================================================================================================
// CanvasCore
//================================================================================================

#include "pch.h"

#include "Canvas.h"
#include "CanvasElement.h"
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
GEMMETHODIMP CCanvas::Initialize()
{
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CCanvas::Uninitialize()
{
    // Shutdown the render queue manager before releasing graphics
    m_RenderQueueManager.Shutdown();
    
    // Iterate still active XCanvasElement objects and report them as leaks
    for(XCanvasElement *pElement : m_ActiveCanvasElements)
    {
        auto pLogger = GetCanvasLogger();

        if(pElement->GetName())
        {
            pLogger->Error("%s leaked, Name: %s", pElement->GetTypeName(), pElement->GetName());
        }
        else
        {
            pLogger->Error("%s leaked", pElement->GetTypeName());
        }
    }
}

//------------------------------------------------------------------------------------------------
CCanvas::~CCanvas()
{
}

//------------------------------------------------------------------------------------------------
template<class _Type>
Gem::Result CCanvas::CreateElement(typename _Type::BaseType **ppElement)
{
    if (!ppElement)
    {
        return Gem::Result::BadPointer;
    }

    try
    {
        Gem::TGemPtr<_Type> pObj;
        Gem::ThrowGemError(Gem::TGenericImpl<_Type>::Create(&pObj, this));
        m_ActiveCanvasElements.emplace(pObj);
        *ppElement = pObj.Detach();
    }
    catch(const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CCanvas::CanvasElementDestroyed(XCanvasElement *pElement)
{
    m_ActiveCanvasElements.erase(pElement);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateScene(XScene **ppScene)
{
    CFunctionSentinel Sentinel("XCanvas::CreateScene");

    return CreateElement<CScene>(ppScene);

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateSceneGraphNode(XSceneGraphNode **ppNode)
{
    CFunctionSentinel Sentinel("XCanvas::CreateSceneGraphNode");

    return CreateElement<CSceneGraphNode>(ppNode);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateCamera(XCamera **ppCamera)
{
    CFunctionSentinel Sentinel("XCanvas::CreateCamera");

    return CreateElement<CCamera>(ppCamera);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateLight(XLight **ppLight)
{
    CFunctionSentinel Sentinel("XCanvas::CreateCamera");

    return CreateElement<CLight>(ppLight);
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
GEMMETHODIMP CCanvas::InitGfx(PCSTR path, HWND hWnd)
{
    CFunctionSentinel Sentinel("XCanvas::InitGfx");

    try
    {
        std::unique_ptr<CCanvasPlugin> pPlugin = std::make_unique<CCanvasPlugin>(path);

        auto pfnCreateCanvasGfxFactory = pPlugin->GetProc<FnCreateGfxFactory>("CreateCanvasGfxFactory");
        if(!pfnCreateCanvasGfxFactory)
        {
            Gem::ThrowGemError(Gem::Result::PluginProcNodFound);
        }

        Gem::TGemPtr<Canvas::XGfxFactory> pFactory;
        pfnCreateCanvasGfxFactory(&pFactory);

        // Create the graphics device
        Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
        Gem::ThrowGemError(pFactory->CreateDevice(&pDevice));

        // Create the graphics context
        Gem::TGemPtr<Canvas::XGfxGraphicsContext> pGfxContext;
        Gem::ThrowGemError(pDevice->CreateGraphicsContext(&pGfxContext));

        // Create the swapchain
        Gem::TGemPtr<Canvas::XGfxSwapChain> pSwapChain;
        Gem::ThrowGemError(pGfxContext->CreateSwapChain(hWnd, true, &pSwapChain, Canvas::GfxFormat::R16G16B16A16_Float, 4));
        // Initialize the render queue manager with the graphics device
        Gem::ThrowGemError(m_RenderQueueManager.Initialize(pDevice.Get()));

        m_pGfxFactory.Attach(pFactory.Detach());
        m_pGfxDevice.Attach(pDevice.Detach());
        m_pGfxContext.Attach(pGfxContext.Detach());
        m_pGfxSwapChain.Attach(pSwapChain.Detach());
        m_pGfxPlugin.swap(pPlugin);
    }
    catch (const Gem::GemError &e)
    {
        Sentinel.SetResultCode(e.Result());
        return e.Result();
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
// Updates application logic and submits work to the graphics engine
GEMMETHODIMP CCanvas::FrameTick()
{
    Gem::Result result = Gem::Result::Success;

    // Elapse time

    // Update scene graph

    // BUGBUG - TEST CODE: REPLACE WITH RENDER QUEUE DISPATCH
    const float ClearColors[2][4] =
    {
        { 1.f, 0.f, 0.f, 0.f },
        { 0.f, 0.f, 1.f, 0.f },
    };

    static UINT clearColorIndex = 0;

    Gem::TGemPtr<Canvas::XGfxSurface> pSurface;
    m_pGfxSwapChain->GetSurface(&pSurface);
    m_pGfxContext->ClearSurface(pSurface, ClearColors[clearColorIndex]);
    clearColorIndex ^= 1;
    m_pGfxContext->FlushAndPresent(m_pGfxSwapChain);
    m_pGfxContext->Wait();
    // BUGBUG - END TEST CODE


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