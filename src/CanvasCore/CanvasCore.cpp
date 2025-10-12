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
GEMMETHODIMP CCanvas::Initialize()
{
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CCanvas::Uninitialize()
{
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
    CFunctionSentinel Sentinel("XCanvas::CreateElement");

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
    CFunctionSentinel Sentinel("CreateCanvas");
    
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
GEMMETHODIMP CCanvas::InitGfx(PCSTR path)
{
    CFunctionSentinel Sentinel("XCanvas::InitGfx");

    try
    {
        std::unique_ptr<CCanvasPlugin> pPlugin = std::make_unique<CCanvasPlugin>(path);

        auto pfnCreateCanvasGfxDeviceFactory = pPlugin->GetProc<FnCreateGfxDeviceFactory>("CreateCanvasGfxDeviceFactory");
        if(!pfnCreateCanvasGfxDeviceFactory)
        {
            Gem::ThrowGemError(Gem::Result::PluginProcNodFound);
        }

        Gem::TGemPtr<XGfxDeviceFactory> pGfxDeviceFactory;
        pfnCreateCanvasGfxDeviceFactory(&pGfxDeviceFactory);

        m_pGfxDeviceFactory.Attach(pGfxDeviceFactory.Detach());
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
GEMMETHODIMP CCanvas::CreateGfxDevice(XGfxDevice **ppGfxDevice)
{
    CFunctionSentinel Sentinel("XCanvas::CreateGfxDevice");

    try
    {
        if(!m_pGfxDeviceFactory)
            Gem::ThrowGemError(Gem::Result::NotFound);

        Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
        Gem::ThrowGemError(m_pGfxDeviceFactory->CreateDevice(&pDevice));

        *ppGfxDevice = pDevice.Detach();
    }
    catch (const Gem::GemError &e)
    {
        Sentinel.SetResultCode(e.Result());
        return e.Result();
    }

    return Gem::Result::Success;
}

}