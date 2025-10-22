//================================================================================================
// CanvasCore
//================================================================================================

#include "pch.h"

#include "Canvas.h"
#include "CanvasElement.h"
#include "Camera.h"
#include "Light.h"
#include "Scene.h"
#include "CanvasGfx.h"

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
Gem::Result CCanvas::Initialize()
{
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
void CCanvas::Uninitialize()
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
GEMMETHODIMP CCanvas::RegisterElement(XCanvasElement *pElement)
{
    return RegisterElementInternal(pElement);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::UnregisterElement(XCanvasElement *pElement)
{
    return UnregisterElementInternal(pElement);
}

//------------------------------------------------------------------------------------------------
Gem::Result CCanvas::RegisterElementInternal(XCanvasElement *pElement)
{
    if (!pElement)
        return Gem::Result::BadPointer;

    std::lock_guard<std::mutex> lock(m_Mutex);

    auto pLogger = GetCanvasLogger();

    // Check if element is already registered
    if (m_ActiveCanvasElements.find(pElement) != m_ActiveCanvasElements.end())
    {
        if (pLogger)
        {
            if (pElement->GetName())
            {
                pLogger->Warn("Element already registered: %s (Name: %s)", 
                               pElement->GetTypeName(), pElement->GetName());
            }
            else
            {
                pLogger->Warn("Element already registered: %s", pElement->GetTypeName());
            }
        }
        return Gem::Result::InvalidArg;
    }

    m_ActiveCanvasElements.emplace(pElement);

    if(pLogger)
    {
        if (pElement->GetName())
        {
            pLogger->Info("Registered element: %s (Name: %s)", 
                           pElement->GetTypeName(), pElement->GetName());
        }
        else
        {
            pLogger->Info("Registered element type: %s", pElement->GetTypeName());
        }
    }
    
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CCanvas::UnregisterElementInternal(XCanvasElement *pElement)
{
    if (!pElement)
        return Gem::Result::BadPointer;

    std::lock_guard<std::mutex> lock(m_Mutex);

    auto pLogger = GetCanvasLogger();

    auto it = m_ActiveCanvasElements.find(pElement);
    if (it == m_ActiveCanvasElements.end())
    {
        if (pLogger)
        {
            if (pElement->GetName())
            {
                pLogger->Warn("Attempted to unregister element that was not registered: %s (Name: %s)", 
                               pElement->GetTypeName(), pElement->GetName());
            }
            else
            {
                pLogger->Warn("Attempted to unregister element that was not registered: %s", 
                               pElement->GetTypeName());
            }
        }
        return Gem::Result::NotFound;
    }

    if(pLogger)
    {
        if (pElement->GetName())
        {
            pLogger->Info("Unregistering element: %s (Name: %s)", 
                           pElement->GetTypeName(), pElement->GetName());
        }
        else
        {
            pLogger->Info("Unregistering element type: %s", pElement->GetTypeName());
        }
    }

    m_ActiveCanvasElements.erase(it);
    
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateScene(XScene **ppScene, PCSTR name)
{
    CFunctionSentinel Sentinel("XCanvas::CreateScene");

    return CreateElement<CScene>(ppScene, name);

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateSceneGraphNode(XSceneGraphNode **ppNode, PCSTR name)
{
    CFunctionSentinel Sentinel("XCanvas::CreateSceneGraphNode");

    return CreateElement<CSceneGraphNode>(ppNode, name);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateCamera(XCamera **ppCamera, PCSTR name)
{
    CFunctionSentinel Sentinel("XCanvas::CreateCamera");

    return CreateElement<CCamera>(ppCamera, name);
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CCanvas::CreateLight(LightType type, XLight **ppLight, PCSTR name)
{
    CFunctionSentinel Sentinel("XCanvas::CreateLight");

    // Create using the standard CreateElement pattern to ensure proper registration
    Gem::Result result = CreateElement<CLight>(ppLight, name, type);
    
    return result;
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

        // Try to register the graphics device for lifecycle tracking if it supports XCanvasElement
        Gem::TGemPtr<Canvas::XCanvasElement> pElement;
        if (SUCCEEDED(pDevice->QueryInterface(&pElement)))
        {
            Gem::ThrowGemError(pElement->Register(this));
        }

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