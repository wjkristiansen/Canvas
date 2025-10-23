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
// Global logger pointer (not owned - provided by application)
QLog::Logger* g_pLogger = nullptr;

//------------------------------------------------------------------------------------------------
CANVAS_API Gem::Result RegisterCanvasLogger(QLog::Logger* pLogger)
{
    if (!pLogger)
        return Gem::Result::BadPointer;
    
    if (g_pLogger)
        return Gem::Result::Fail;

    g_pLogger = pLogger;

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
CANVAS_API QLog::Logger *GetCanvasLogger()
{
    return g_pLogger;
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

GEMMETHODIMP CCanvas::LoadPlugin(PCSTR path, XCanvasPlugin **ppPlugin)
{
    CFunctionSentinel Sentinel("XCanvas::LoadPlugin");

    try
    {
        auto &module = m_PluginModules.emplace_back(path);
        Gem::ThrowGemError(module.LoadPlugin(ppPlugin));
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

} // namespace Canvas